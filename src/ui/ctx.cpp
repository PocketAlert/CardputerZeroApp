#include "ctx.h"

#include "asset_manager.h"
#include "demo_backend.h"
#include "device_status.h"
#include "modals.h"
#include "notify.h"
#include "i18n.h"
#include "parse.h"
#include "screen.h"
#include "screens.h"
#include "screenshot_service.h"
#include "theme.h"
#include "tr.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace ui {
namespace {

constexpr uint32_t kTickMs = 1000;
constexpr uint32_t kDeviceStatusMs = 10000;
constexpr uint32_t kMaxPollBackoffMs = 300000;
constexpr size_t kMaxMessageChanges = 256;

constexpr uint32_t kToastMessageMs = 4500;
constexpr uint32_t kToastErrorMs = 3800;
constexpr uint32_t kToastInfoMs = 2400;
constexpr int kToastMargin = 8;
constexpr int kToastTextInset = 50;

std::shared_ptr<pa::HttpTransport> demo_transport() {
    // One instance per process so demo data survives screen and API rebuilds.
    static auto transport = std::make_shared<pa::DemoTransport>(35, true);
    return transport;
}

std::string clock_text() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buffer[8];
    std::strftime(buffer, sizeof(buffer), "%H:%M", &tm);
    return buffer;
}

} // namespace

Ctx::Ctx(app::AssetManager& assets, std::string framebuffer)
    : assets_(assets), backlight_(std::move(framebuffer)) {
    pa::load_config(config_);
    if (const char* demo = std::getenv("POCKETALERT_DEMO"); demo && demo[0] == '1') {
        config_.demo = true;
    }
    set_palette(config_.theme);
    load_language();
    configure_api();
    ui_lock_ = std::make_unique<pa::UiLock>();
}

Ctx::~Ctx() {
    alive_.reset();
    if (tick_timer_) lv_timer_delete(tick_timer_);
    hide_toast();
    if (shade_) lv_obj_delete(shade_);
    modals_.clear();
    closed_modals_.clear();
    if (!stack_.empty()) lv_screen_load(lv_obj_create(nullptr));
    while (!stack_.empty()) stack_.pop_back();
}

void Ctx::start() {
    last_input_ms_ = lv_tick_get();
    tick_timer_ = lv_timer_create(tick_cb, kTickMs, this);
    update_status();
    if (signed_in()) {
        reset(make_inbox(*this));
        offer_tour();
        load_lookups();
        register_device();
    } else {
        reset(make_login(*this));
    }
    apply_nav();
}

void Ctx::save_config() {
    std::string error;
    if (!pa::save_config(config_, &error)) {
        toast(tr("ui.toast.settingsNotSaved", {{"error", error}}), ToastKind::Error);
    }
}

void Ctx::configure_api() {
    ++api_generation_;
    // A poll of the previous account may still be in flight; its result is
    // dropped by generation, and the next poll must not wait for it.
    polling_ = false;
    poll_failures_ = 0;
    registering_device_ = false;
    std::shared_ptr<pa::HttpTransport> transport;
    if (config_.demo) {
        transport = demo_transport();
    } else {
        transport = std::make_shared<pa::CurlTransport>(std::string("pocketalert-cardputer/") + PA_VERSION);
    }
    pa::Credentials credentials;
    credentials.api_key = config_.api_key;
    api_ = std::make_shared<pa::Api>(transport, pa::kDefaultBaseUrl, credentials);
}

void Ctx::sign_in_with_key(const std::string& api_key, const std::string& email) {
    config_.api_key = api_key;
    config_.account_email = email;
    config_.demo = false;
    config_.device_tid.clear();
    save_config();
    configure_api();
    pa::save_state(pa::NotifyState{});
    reset(make_inbox(*this));
    offer_tour();
    load_lookups();
    poll_now();
    device_attempt_ms_ = 0;
    register_device();
}

void Ctx::start_demo() {
    config_.demo = true;
    config_.account_email = "demo@pocketalert.app";
    save_config();
    configure_api();
    pa::save_state(pa::NotifyState{});
    reset(make_inbox(*this));
    offer_tour();
    load_lookups();
    poll_now();
}

void Ctx::offer_tour() {
    if (!config_.onboarded || config_.tour_on_launch) push(make_welcome(*this));
}

void Ctx::sign_out() {
    config_.api_key.clear();
    config_.account_email.clear();
    config_.device_tid.clear();
    config_.demo = false;
    save_config();
    configure_api();
    pa::save_state(pa::NotifyState{});
    apps_.clear();
    devices_.clear();
    status_.unread = 0;
    reset(make_login(*this));
}

// ---------------------------------------------------------------- navigation

void Ctx::push(std::unique_ptr<Screen> screen) {
    nav_ops_.push_back({NavOp::Kind::Push, std::move(screen), nullptr});
}

void Ctx::pop(const Screen* self) {
    nav_ops_.push_back({NavOp::Kind::Pop, nullptr, self});
}

void Ctx::reset(std::unique_ptr<Screen> screen) {
    nav_ops_.push_back({NavOp::Kind::Reset, std::move(screen), nullptr});
}

void Ctx::apply_nav() {
    while (!nav_ops_.empty()) {
        auto op = std::move(nav_ops_.front());
        nav_ops_.erase(nav_ops_.begin());
        switch (op.kind) {
            case NavOp::Kind::Push:
                op.screen->create();
                lv_screen_load(op.screen->root());
                stack_.push_back(std::move(op.screen));
                break;
            case NavOp::Kind::Pop:
                if (stack_.size() > 1 && (!op.expected || stack_.back().get() == op.expected)) {
                    auto old = std::move(stack_.back());
                    stack_.pop_back();
                    drop_modals_of(old.get());
                    lv_screen_load(stack_.back()->root());
                    old.reset();
                    refresh_chrome(stack_.back().get());
                    stack_.back()->on_resume();
                }
                break;
            case NavOp::Kind::Reset: {
                for (auto& modal : modals_) closed_modals_.push_back(std::move(modal));
                modals_.clear();
                op.screen->create();
                lv_screen_load(op.screen->root());
                while (!stack_.empty()) stack_.pop_back();
                stack_.push_back(std::move(op.screen));
                break;
            }
        }
    }
}

// A modal's callbacks point into the screen that opened it; it must not
// outlive that screen.
void Ctx::drop_modals_of(const Screen* screen) {
    for (auto it = modals_.begin(); it != modals_.end();) {
        if ((*it)->owner() == screen) {
            closed_modals_.push_back(std::move(*it));
            it = modals_.erase(it);
        } else {
            ++it;
        }
    }
}

void Ctx::open_modal(std::unique_ptr<Modal> modal) {
    // A screen that already asked to leave hands the modal to the one below,
    // e.g. "Webhook created" shown over the list the form returns to.
    const Screen* owner = current();
    for (const auto& op : nav_ops_) {
        if (op.kind == NavOp::Kind::Pop && op.expected == owner && stack_.size() > 1) {
            owner = stack_[stack_.size() - 2].get();
        }
    }
    modal->set_owner(owner);
    modals_.push_back(std::move(modal));
    if (toast_) lv_obj_move_foreground(toast_);
}

void Ctx::close_modal(Modal* modal) {
    for (auto it = modals_.begin(); it != modals_.end(); ++it) {
        if (it->get() == modal) {
            closed_modals_.push_back(std::move(*it));
            modals_.erase(it);
            return;
        }
    }
}

bool Ctx::frame() {
    const bool queued = !nav_ops_.empty();
    const bool drained = worker_.drain_ui();
    const bool worked = queued || drained || !nav_ops_.empty() || !closed_modals_.empty();
    apply_nav();
    closed_modals_.clear();
    return worked;
}

void Ctx::note_message_change(MessageChange change) {
    message_changes_.push_back(std::move(change));
    if (message_changes_.size() > kMaxMessageChanges) {
        const size_t drop = kMaxMessageChanges / 2;
        message_changes_.erase(message_changes_.begin(), message_changes_.begin() + static_cast<std::ptrdiff_t>(drop));
        changes_base_ += drop;
    }
}

bool Ctx::take_message_changes(size_t& cursor, std::vector<MessageChange>& out) const {
    const bool complete = cursor >= changes_base_;
    for (size_t i = std::max(cursor, changes_base_); i < message_changes_end(); ++i) {
        out.push_back(message_changes_[i - changes_base_]);
    }
    cursor = message_changes_end();
    return complete;
}

// ---------------------------------------------------------------- feedback

void Ctx::toast(const std::string& text, ToastKind kind, int level, const std::string& sub) {
    const auto& t = theme();
    hide_toast();
    toast_ = make_box(lv_layer_top());
    lv_obj_set_width(toast_, kScreenW - 2 * kToastMargin);
    lv_obj_set_height(toast_, LV_SIZE_CONTENT);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, kHeaderH + 3);
    lv_obj_set_style_bg_color(toast_, t.p.raised, 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast_, 5, 0);
    lv_obj_set_style_pad_hor(toast_, 7, 0);
    lv_obj_set_style_pad_ver(toast_, 5, 0);
    lv_obj_set_style_shadow_width(toast_, 12, 0);
    lv_obj_set_style_shadow_color(toast_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(toast_, LV_OPA_40, 0);

    lv_color_t accent = t.p.accent;
    const char* glyph = icon::kInfo;
    switch (kind) {
        case ToastKind::Success: accent = t.p.ok; glyph = icon::kCheckCircle; break;
        case ToastKind::Error: accent = t.p.danger; glyph = icon::kWarningCircle; break;
        case ToastKind::Message: accent = level_color(level); glyph = icon::kBellRinging; break;
        default: break;
    }
    lv_obj_set_style_border_side(toast_, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(toast_, 3, 0);
    lv_obj_set_style_border_color(toast_, accent, 0);
    set_row_flex(toast_, 6);
    lv_obj_set_flex_align(toast_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    make_icon(toast_, glyph, accent, t.f.icon);
    auto* column = make_box(toast_);
    lv_obj_set_size(column, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(column, 1);
    set_column_flex(column, 1);
    auto* label = make_label(column, text, kind == ToastKind::Message ? t.f.body_bold : t.f.body, t.p.text);
    lv_obj_set_width(label, kScreenW - kToastTextInset);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    if (!sub.empty()) {
        auto* sub_label = make_label(column, sub, t.f.small, t.p.muted);
        lv_obj_set_width(sub_label, kScreenW - kToastTextInset);
        single_line(sub_label);
    }

    const uint32_t duration = kind == ToastKind::Message ? kToastMessageMs
                              : kind == ToastKind::Error ? kToastErrorMs
                                                         : kToastInfoMs;
    toast_timer_ = lv_timer_create(toast_timer_cb, duration, this);
    lv_timer_set_repeat_count(toast_timer_, 1);
    lv_timer_set_auto_delete(toast_timer_, false);
}

void Ctx::hide_toast() {
    if (toast_timer_) {
        lv_timer_delete(toast_timer_);
        toast_timer_ = nullptr;
    }
    if (toast_) {
        lv_obj_delete(toast_);
        toast_ = nullptr;
    }
}

void Ctx::toast_timer_cb(lv_timer_t* timer) {
    static_cast<Ctx*>(lv_timer_get_user_data(timer))->hide_toast();
}

void Ctx::show_error(const pa::ApiError& error, const std::string& prefix) {
    std::string text = prefix.empty() ? error.text() : tr("ui.error.withPrefix", {{"prefix", prefix}, {"error", error.text()}});
    std::fprintf(stderr, "[pocketalert] api error %ld: %s\n", error.status, text.c_str());
    if (error.status == 401 && !config_.demo) {
        text = tr("ui.error.checkKey", {{"error", text}});
    }
    toast(text, ToastKind::Error);
}

// ---------------------------------------------------------------- shared data

void Ctx::load_lookups(std::function<void()> done) {
    using Lookups = std::pair<pa::Result<std::vector<pa::Application>>, pa::Result<std::vector<pa::Device>>>;
    const auto generation = api_generation_;
    run<Lookups>(
        alive_, [](pa::Api& api) { return Lookups{api.applications(), api.devices()}; },
        [this, done, generation](Lookups& result) {
            if (generation != api_generation_) return;
            if (result.first.ok) apps_ = result.first.value;
            if (result.second.ok) devices_ = result.second.value;
            if (done) done();
        });
}

// Lists this Cardputer under the account's devices, the way the phone app does
// at sign-in. It has no push token: alerts reach it by polling.
void Ctx::register_device() {
    if (config_.demo || config_.api_key.empty() || !config_.device_tid.empty() || registering_device_) {
        return;
    }
    const auto now = lv_tick_get();
    if (device_attempt_ms_ != 0 && now - device_attempt_ms_ < 10U * 60U * 1000U) {
        return;
    }
    const auto serial = pa::hardware_id();
    if (serial.empty()) {
        return;
    }
    registering_device_ = true;
    device_attempt_ms_ = now == 0 ? 1 : now;
    auto api = api_;
    auto post = worker_.ui_poster();
    std::weak_ptr<bool> owner = alive_;
    const auto generation = api_generation_;
    worker_.post([api, post, owner, serial, generation, this]() {
        pa::Result<std::string> outcome;
        auto existing = api->device_by_serial(serial);
        if (existing.ok) {
            outcome.ok = true;
            outcome.value = existing.value.tid;
        } else if (existing.error.status == 404) {
            outcome = api->register_device(serial, "Cardputer Zero", "cardputer", PA_VERSION);
        } else {
            outcome.error = existing.error;
        }
        post([outcome, owner, generation, this]() {
            if (!owner.lock() || generation != api_generation_) return;
            registering_device_ = false;
            if (!outcome.ok) {
                std::fprintf(stderr, "[pocketalert] device registration failed %ld: %s\n", outcome.error.status,
                             outcome.error.text().c_str());
                return;
            }
            if (outcome.value.empty()) return;
            config_.device_tid = outcome.value;
            save_config();
            load_lookups();
        });
    });
}

std::string Ctx::app_color(const std::string& name) const {
    for (const auto& app : apps_) {
        if (app.name == name || app.tid == name) return app.color;
    }
    return {};
}

std::vector<std::pair<std::string, std::string>> Ctx::app_options(const std::string& none_label) const {
    std::vector<std::pair<std::string, std::string>> options{{"", none_label}};
    for (const auto& app : apps_) options.emplace_back(app.tid, app.name);
    return options;
}

std::vector<std::pair<std::string, std::string>> Ctx::device_options() const {
    std::vector<std::pair<std::string, std::string>> options{{"", tr("ui.options.allDevices")}};
    for (const auto& device : devices_) options.emplace_back(device.tid, device.name);
    return options;
}

// ---------------------------------------------------------------- polling

void Ctx::poll_now() {
    last_poll_ms_ = 0;
    poll_failures_ = 0;
    poll();
}

void Ctx::poll() {
    if (!signed_in() || polling_) {
        return;
    }
    polling_ = true;
    last_poll_ms_ = lv_tick_get();
    auto api = api_;
    auto post = worker_.ui_poster();
    std::weak_ptr<bool> owner = alive_;
    const auto generation = api_generation_;
    // Background polls stay out of the busy indicator; only user actions spin it.
    worker_.post([api, post, owner, generation, this]() {
        pa::MessageQuery query;
        // Only new arrivals matter here; the inbox pages the rest on demand.
        query.size = 20;
        auto result = std::make_shared<pa::Result<pa::MessagePage>>();
        try {
            *result = api->messages(query);
        } catch (const std::exception& e) {
            detail::mark_failed(*result, e.what());
        }
        post([result, owner, generation, this]() {
            if (!owner.lock() || generation != api_generation_) return;
            polling_ = false;
            if (!result->ok) {
                // Logged once per outage; the back-off keeps retries sparse.
                if (poll_failures_++ == 0) {
                    std::fprintf(stderr, "[pocketalert] poll failed %ld: %s\n", result->error.status,
                                 result->error.text().c_str());
                }
                status_.offline = result->error.network();
                if (current()) refresh_chrome(current());
                return;
            }
            poll_failures_ = 0;
            status_.offline = false;
            register_device();
            pa::NotifyState state;
            pa::load_state(state);
            const auto before = state;
            auto detection = pa::detect_new(result->value.data, state);
            // The state file sits on the SD card: rewrite it only when it changed.
            if (state != before) pa::save_state(state);
            status_.unread = detection.unread;
            status_.unread_overflow = result->value.has_more &&
                                      detection.unread >= static_cast<int>(result->value.data.size());
            if (!detection.fresh.empty()) {
                // A pager lights up for a new alert.
                wake();
                const auto& newest = detection.fresh.front();
                const auto loudest = pa::loudest_level(detection.fresh);
                std::string sub = newest.application.empty() ? "" : newest.application;
                if (detection.fresh.size() > 1) {
                    sub += (sub.empty() ? "" : " · ") +
                           tr("ui.toast.moreMessages", {{"count", std::to_string(detection.fresh.size() - 1)}});
                }
                toast(newest.title, ToastKind::Message, newest.priority, sub);
                if (config_.sound && loudest > -2 && loudest >= config_.notify_min_level) {
                    pa::play_sound(loudest >= 2 ? pa::Sound::Critical : pa::Sound::Alert);
                }
                if (current()) current()->on_new_messages(detection.fresh);
            }
            if (current()) refresh_chrome(current());
        });
    });
}

// With the screen off nobody is looking: check as rarely as the notifier
// would. Failures back off exponentially instead of retrying every interval.
uint32_t Ctx::poll_interval_ms() const {
    int seconds = std::max(5, config_.poll_seconds);
    if (power_ == platform::ScreenPower::Off) seconds = std::max(seconds, config_.notify_poll_seconds);
    uint32_t interval = static_cast<uint32_t>(seconds) * 1000U;
    if (poll_failures_ > 0) {
        interval = std::min(kMaxPollBackoffMs, interval << std::min(poll_failures_, 5));
    }
    return interval;
}

void Ctx::tick_cb(lv_timer_t* timer) {
    static_cast<Ctx*>(lv_timer_get_user_data(timer))->tick();
}

void Ctx::tick() {
    const auto now = lv_tick_get();
    const bool screen_on = power_ != platform::ScreenPower::Off;
    if (screen_on && (last_device_status_ms_ == 0 || now - last_device_status_ms_ >= kDeviceStatusMs)) {
        last_device_status_ms_ = now;
        update_status();
        if (current()) refresh_chrome(current());
    }
    if (signed_in() && (last_poll_ms_ == 0 || now - last_poll_ms_ >= poll_interval_ms())) {
        poll();
    }

    const uint32_t idle = now - last_input_ms_;
    const auto after = [&](int seconds) { return seconds > 0 && idle >= static_cast<uint32_t>(seconds) * 1000U; };
    auto target = platform::ScreenPower::On;
    if (after(config_.screen_off_seconds)) {
        target = platform::ScreenPower::Off;
    } else if (after(config_.screen_dim_seconds)) {
        target = platform::ScreenPower::Dim;
    }
    // Only ever darken here; brightening is wake()'s job.
    if (static_cast<int>(target) > static_cast<int>(power_)) set_power(target);
}

void Ctx::wake() {
    last_input_ms_ = lv_tick_get();
    if (power_ != platform::ScreenPower::On) set_power(platform::ScreenPower::On);
}

void Ctx::set_power(platform::ScreenPower power) {
    const bool was_off = power_ == platform::ScreenPower::Off;
    power_ = power;
    const bool hardware = backlight_.set(power);
    if (power == platform::ScreenPower::On || hardware) {
        if (shade_) {
            lv_obj_delete(shade_);
            shade_ = nullptr;
        }
    } else {
        if (!shade_) {
            shade_ = make_box(lv_layer_sys());
            lv_obj_set_size(shade_, kScreenW, kScreenH);
            lv_obj_set_style_bg_color(shade_, lv_color_hex(0x000000), 0);
        }
        lv_obj_set_style_bg_opa(shade_, power == platform::ScreenPower::Off ? LV_OPA_COVER : LV_OPA_60, 0);
    }
    if (power == platform::ScreenPower::On && was_off) {
        // The status bar was left alone while dark; bring it up to date.
        last_device_status_ms_ = 0;
        update_status();
        if (current()) refresh_chrome(current());
        lv_obj_invalidate(lv_screen_active());
    }
}

void Ctx::update_status() {
    const auto device = platform::read_device_status();
    status_.wifi_present = device.wifi_present;
    status_.wifi_connected = device.wifi_connected;
    status_.battery = device.battery_present ? device.battery_percent : -1;
    status_.charging = device.battery_charging;
    status_.clock = clock_text();
    status_.demo = config_.demo;
}

void Ctx::refresh_chrome(Screen* screen) {
    if (!screen) return;
    status_.demo = config_.demo;
    screen->chrome().set_status(status_);
    screen->chrome().set_busy(busy_ > 0);
}

void Ctx::set_busy_delta(int delta) {
    busy_ = std::max(0, busy_ + delta);
    if (current()) current()->chrome().set_busy(busy_ > 0);
}

void Ctx::load_language() {
    pa::i18n::load(assets_.resolve("i18n"), pa::i18n::resolve(config_.language));
}

void Ctx::apply_theme(bool reopen_settings) {
    set_palette(config_.theme);
    load_language();
    apply_to_display(lv_display_get_default());
    reset(make_inbox(*this));
    push(make_menu(*this));
    if (reopen_settings) push(make_settings(*this));
}

// ---------------------------------------------------------------- keys

void Ctx::handle_key(uint32_t code, bool long_pressed, bool released, uint32_t repeat) {
    if (swallowed_key_ != 0) {
        if (code == swallowed_key_ && released) swallowed_key_ = 0;
        return;
    }
    last_input_ms_ = lv_tick_get();
    // The first key on a dark screen only turns it back on.
    if (power_ != platform::ScreenPower::On && !released) {
        swallowed_key_ = code;
        set_power(platform::ScreenPower::On);
        return;
    }
    if (repeat > 0) {
        const auto key = make_key(code);
        const bool repeatable = key.up() || key.down() || key.left() || key.right() || key.page_up() ||
                                key.page_down() || key.backspace() || key.is(LV_KEY_DEL);
        if (!repeatable) {
            return;
        }
        // The repeat rate already climbs; past that the list moves in strides.
        const int steps = repeat < 20 ? 1 : repeat < 45 ? 2 : 4;
        for (int i = 0; i < steps; ++i) {
            dispatch(key);
        }
        return;
    }
    if (code == LV_KEY_ESC) {
        if (long_pressed) {
            esc_long_fired_ = true;
            request_quit();
            return;
        }
        if (released) {
            if (esc_long_fired_) {
                esc_long_fired_ = false;
                return;
            }
            dispatch(make_key(code));
        }
        return;
    }
    if (released || long_pressed) {
        return;
    }
    if (code == platform::kKeyHelp) {
        show_help();
        return;
    }
    if (code == platform::kKeyPrintScreen) {
        take_screenshot();
        return;
    }
    dispatch(make_key(code));
}

void Ctx::dispatch(const Key& key) {
    if (!modals_.empty()) {
        auto* modal = modals_.back().get();
        if (!modal->on_key(key) && key.esc()) {
            close_modal(modal);
        }
        return;
    }
    auto* screen = current();
    if (!screen) return;
    if (screen->on_key(key)) return;
    if (key.esc()) {
        if (stack_.size() > 1) {
            pop(screen);
        } else {
            toast(tr("ui.toast.holdEscToExit"));
        }
    }
}

void Ctx::show_help() {
    std::string text;
    if (auto* screen = current()) {
        text = screen->help();
    }
    if (!text.empty()) text += "\n\n";
    text += tr("ui.help.global");
    open_modal(info_modal(*this, tr("ui.help.title"), text, "", icon::kInfo));
}

void Ctx::take_screenshot() {
    const auto result = platform::screenshot::capture_active_screen();
    if (result.success) {
        toast(tr("ui.toast.screenshotSaved"), ToastKind::Success);
    } else {
        toast(tr("ui.toast.screenshotFailed", {{"error", result.error}}), ToastKind::Error);
    }
}

} // namespace ui
