#include "form_screen.h"
#include "resource_screen.h"

#include "config.h"
#include "notify.h"
#include "parse.h"
#include "tr.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>

namespace ui {
namespace {

const std::array<const char*, 5> kStatuses = {"pending", "all", "sent", "canceled", "failed"};

std::string until(const std::string& rfc3339) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(rfc3339.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return rfc3339;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    const auto seconds = static_cast<long long>(timegm(&tm) - std::time(nullptr));
    if (seconds <= 0) return tr("account.duration.now");
    const auto hours = seconds / 3600;
    const auto minutes = std::to_string((seconds % 3600) / 60);
    if (hours > 0) return tr("account.duration.hoursMinutes", {{"hours", std::to_string(hours)}, {"minutes", minutes}});
    return tr("account.duration.minutes", {{"minutes", minutes}});
}

std::string status_label(const std::string& status) {
    if (std::find(kStatuses.begin(), kStatuses.end(), status) == kStatuses.end()) return status;
    return tr("scheduled.status." + status);
}

// ---------------------------------------------------------------- scheduled

class ScheduledScreen final : public ResourceScreen<pa::ScheduledMessage> {
public:
    explicit ScheduledScreen(Ctx& ctx, std::shared_ptr<std::atomic<int>> status = std::make_shared<std::atomic<int>>(0))
        : ResourceScreen(ctx, tr("scheduled.title"), 32,
                         [status](pa::Api& api) { return api.scheduled(kStatuses[static_cast<size_t>(status->load())]); },
                         tr("scheduled.empty.title"), tr("scheduled.empty.hint"), icon::kCalendar),
          status_(std::move(status)) {}

protected:
    void setup() override {
        chrome_.set_keys({tr("scheduled.keys.new"), tr("scheduled.keys.status"), "", tr("scheduled.keys.info"),
                          tr("scheduled.keys.cancel")});
        update_title();
    }

    void loaded() override { update_title(); }

    void build_row(lv_obj_t* row, const pa::ScheduledMessage& m) override {
        const auto& t = theme();
        lv_color_t color = t.p.muted;
        if (m.status == "pending") color = t.p.accent;
        else if (m.status == "sent") color = t.p.ok;
        else if (m.status == "failed") color = t.p.danger;
        std::string sub = m.deliver_at;
        if (!m.application.empty()) sub += "  ·  " + m.application;
        standard_row(row, icon::kClock, level_color(m.priority), m.title, sub, status_label(m.status), color);
    }

    bool handle(const Key& key) override {
        const int slot = key.slot();
        if (slot == 0 || key.is_char('n')) {
            ctx_.push(make_compose(ctx_));
            return true;
        }
        if (slot == 1 || key.right() || key.left()) {
            const int n = static_cast<int>(kStatuses.size());
            status_->store((status_->load() + (key.left() ? n - 1 : 1)) % n);
            items_.clear();
            render();
            update_title();
            reload();
            return true;
        }
        auto* m = selected();
        if (!m) return false;
        if (slot == 3 || key.enter()) {
            std::string text = m->message + "\n\n" +
                               (m->tz.empty() ? tr("scheduled.info.deliver", {{"time", m->deliver_at}})
                                              : tr("scheduled.info.deliverTz", {{"time", m->deliver_at}, {"tz", m->tz}})) +
                               "\n" + tr("scheduled.info.status", {{"status", status_label(m->status)}}) + "\n" +
                               tr("scheduled.info.level", {{"level", level_label(m->priority)}});
            if (!m->application.empty()) text += "\n" + tr("scheduled.info.application", {{"name", m->application}});
            if (!m->device.empty()) text += "\n" + tr("scheduled.info.device", {{"name", m->device}});
            for (const auto& action : m->actions) {
                text += "\n" + tr("scheduled.info.action", {{"action", pa::format_action_spec(action)}});
            }
            ctx_.open_modal(info_modal(ctx_, m->title, text, "", icon::kClock));
            return true;
        }
        if (slot == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            if (m->status != "pending") {
                ctx_.toast(tr("scheduled.toast.onlyPending"), ToastKind::Error);
                return true;
            }
            const auto tid = m->tid;
            ctx_.open_modal(confirm_modal(ctx_, tr("scheduled.confirm.cancel.title"), one_line(m->title, 90), tr("scheduled.confirm.cancel.button"), true, [this, tid] {
                run_once<pa::Result<pa::Empty>>([tid](pa::Api& api) { return api.cancel_scheduled(tid); },
                                           [this](pa::Result<pa::Empty>& result) {
                                               if (!result.ok) {
                                                   ctx_.show_error(result.error);
                                                   return;
                                               }
                                               ctx_.toast(tr("scheduled.toast.canceled"), ToastKind::Success);
                                               reload();
                                           });
            }));
            return true;
        }
        return false;
    }

    std::string help() const override { return scheduled_help(); }

private:
    void update_title() {
        chrome_.set_subtitle(status_label(kStatuses[static_cast<size_t>(status_->load())]), theme().p.accent);
    }

    std::shared_ptr<std::atomic<int>> status_;
};

// ---------------------------------------------------------------- account

struct AccountData {
    pa::Result<pa::Profile> profile;
    pa::Result<pa::Usage> usage;
    pa::Result<std::vector<pa::DayStat>> stats;
};

class AccountScreen final : public Screen {
public:
    explicit AccountScreen(Ctx& ctx) : Screen(ctx, tr("account.title")) {}

    void build() override {
        chrome_.set_keys({tr("account.keys.refresh"), "", "", "", ""});
        render();
        load();
    }

    bool on_key(const Key& key) override {
        if (key.slot() == 0 || key.is_char('g')) {
            load();
            return true;
        }
        if (scroll_ && (key.down() || key.up())) {
            lv_obj_scroll_by_bounded(scroll_, 0, key.down() ? -30 : 30, LV_ANIM_OFF);
            return true;
        }
        return false;
    }

private:
    void load() {
        run<AccountData>([](pa::Api& api) { return AccountData{api.profile(), api.usage(), api.stats()}; },
                         [this](AccountData& data) {
                             if (!data.profile.ok) ctx_.show_error(data.profile.error);
                             data_ = std::move(data);
                             loaded_ = true;
                             render();
                         });
    }

    void render() {
        const auto& t = theme();
        auto* body = chrome_.body();
        lv_obj_clean(body);
        scroll_ = make_box(body);
        lv_obj_set_size(scroll_, kScreenW, kBodyH);
        lv_obj_add_flag(scroll_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(scroll_, LV_DIR_VER);
        lv_obj_set_style_pad_hor(scroll_, 8, 0);
        lv_obj_set_style_pad_ver(scroll_, 5, 0);
        set_column_flex(scroll_, 5);
        if (!loaded_) {
            make_label(scroll_, tr("app.loading"), t.f.body, t.p.muted);
            return;
        }

        if (data_.profile.ok) {
            const auto& p = data_.profile.value;
            auto* who = make_box(scroll_);
            lv_obj_set_size(who, lv_pct(100), LV_SIZE_CONTENT);
            set_row_flex(who, 8);
            make_icon(who, icon::kUser, t.p.accent, t.f.icon_big);
            auto* column = make_box(who);
            lv_obj_set_size(column, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(column, 1);
            set_column_flex(column, 0);
            auto* email = make_label(column, p.email, t.f.body_bold, t.p.text);
            lv_obj_set_width(email, 200);
            single_line(email);
            std::string sub = p.username;
            if (!p.timezone.empty()) sub += (sub.empty() ? "" : "  ·  ") + p.timezone;
            make_label(column, sub, t.f.small, t.p.muted);
            const bool paid = p.subscription_status == "active" || p.subscription_status == "trialing";
            make_label(who, tr(paid ? "account.plan.pro" : "account.plan.free"), t.f.small_bold, paid ? t.p.ok : t.p.muted);
        }

        if (data_.usage.ok) {
            const auto& u = data_.usage.value;
            auto* block = make_box(scroll_);
            lv_obj_set_size(block, lv_pct(100), LV_SIZE_CONTENT);
            set_column_flex(block, 3);
            auto* line = make_box(block);
            lv_obj_set_size(line, lv_pct(100), LV_SIZE_CONTENT);
            set_row_flex(line, 6);
            make_label(line, tr("account.usage.sentToday"), t.f.small, t.p.muted);
            auto* spacer = make_box(line);
            lv_obj_set_flex_grow(spacer, 1);
            lv_obj_set_height(spacer, 1);
            const std::string count = std::to_string(u.used) + (u.unlimited || !u.limit ? "" : " / " + std::to_string(*u.limit));
            make_label(line, count, t.f.body_bold, t.p.text);

            if (!u.unlimited && u.limit && *u.limit > 0) {
                const int width = kScreenW - 16;
                auto* track = make_box(block);
                lv_obj_set_size(track, width, 6);
                lv_obj_set_style_radius(track, 3, 0);
                lv_obj_set_style_bg_color(track, t.p.raised, 0);
                lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
                const double ratio = std::min(1.0, static_cast<double>(u.used) / *u.limit);
                auto* fill = make_box(track);
                lv_obj_set_size(fill, std::max(3, static_cast<int>(width * ratio)), 6);
                lv_obj_set_style_radius(fill, 3, 0);
                lv_obj_set_style_bg_color(fill, ratio > 0.9 ? t.p.danger : ratio > 0.7 ? t.p.warn : t.p.accent, 0);
                lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            }
            std::string foot = u.unlimited ? std::string(tr("account.usage.unlimited"))
                                           : tr("account.usage.resetsIn", {{"time", until(u.resets_at)}});
            if (u.rejected > 0) foot += "  ·  " + tr("account.usage.rejected", {{"count", std::to_string(u.rejected)}});
            make_label(block, foot, t.f.small, t.p.faint);
        }

        if (data_.stats.ok && !data_.stats.value.empty()) {
            auto stats = data_.stats.value;
            if (stats.size() > 7) stats.erase(stats.begin(), stats.end() - 7);
            int peak = 1;
            for (const auto& s : stats) peak = std::max(peak, s.count);
            make_label(scroll_, tr("account.stats.lastDays"), t.f.tiny, t.p.faint);
            auto* chart = make_box(scroll_);
            lv_obj_set_size(chart, lv_pct(100), 56);
            lv_obj_set_flex_flow(chart, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(chart, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
            for (size_t i = 0; i < stats.size(); ++i) {
                const auto& s = stats[i];
                auto* column = make_box(chart);
                lv_obj_set_size(column, 34, 56);
                lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(column, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_row(column, 1, 0);
                make_label(column, std::to_string(s.count), t.f.tiny, t.p.muted);
                auto* bar = make_box(column);
                lv_obj_set_size(bar, 16, std::max(2, 30 * s.count / peak));
                lv_obj_set_style_radius(bar, 2, 0);
                lv_obj_set_style_bg_color(bar, i + 1 == stats.size() ? t.p.coral : t.p.accent, 0);
                lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
                make_label(column, s.date.size() >= 10 ? s.date.substr(8, 2) + "." + s.date.substr(5, 2) : s.date,
                           t.f.tiny, t.p.faint);
            }
        }
    }

    AccountData data_;
    bool loaded_ = false;
    lv_obj_t* scroll_ = nullptr;
};

// ---------------------------------------------------------------- settings

class SettingsScreen final : public FormScreen {
public:
    explicit SettingsScreen(Ctx& ctx) : FormScreen(ctx, tr("settings.title")) {}

    void build() override {
        chrome_.set_keys({tr("app.save"), tr("settings.keys.test"), "", "", tr("app.back")});
        form_.create(chrome_.body());
        attach(form_);
        const auto& c = ctx_.config();
        std::vector<Field> fields;
        fields.push_back({"poll", tr("settings.fields.poll"), Field::Kind::Choice, std::to_string(c.poll_seconds),
                          {{"10", tr("settings.options.check.s10")},
                           {"15", tr("settings.options.check.s15")},
                           {"30", tr("settings.options.check.s30")},
                           {"60", tr("settings.options.check.m1")}}});
        Field sound{"sound", tr("settings.fields.sound"), Field::Kind::Toggle};
        sound.on = c.sound;
        fields.push_back(sound);
        fields.push_back({"min_level", tr("settings.fields.minLevel"), Field::Kind::Choice, std::to_string(c.notify_min_level),
                          {{"-1", tr("settings.options.minLevel.low")},
                           {"0", tr("settings.options.minLevel.default")},
                           {"1", tr("settings.options.minLevel.high")},
                           {"2", tr("settings.options.minLevel.critical")}}});
        Field notifier{"notifier", tr("settings.fields.notifier"), Field::Kind::Toggle};
        notifier.on = c.notifier_enabled;
        fields.push_back(notifier);
        fields.push_back({"notify_poll", tr("settings.fields.notifyPoll"), Field::Kind::Choice,
                          std::to_string(c.notify_poll_seconds),
                          {{"30", tr("settings.options.check.s30")},
                           {"60", tr("settings.options.check.m1")},
                           {"120", tr("settings.options.check.m2")},
                           {"300", tr("settings.options.check.m5")}}});
        fields.push_back({"dim", tr("settings.fields.dim"), Field::Kind::Choice, std::to_string(c.screen_dim_seconds),
                          {{"15", tr("settings.options.after.s15")},
                           {"30", tr("settings.options.after.s30")},
                           {"60", tr("settings.options.after.m1")},
                           {"120", tr("settings.options.after.m2")},
                           {"0", tr("settings.options.never")}}});
        fields.push_back({"off", tr("settings.fields.off"), Field::Kind::Choice, std::to_string(c.screen_off_seconds),
                          {{"30", tr("settings.options.after.s30")},
                           {"60", tr("settings.options.after.m1")},
                           {"120", tr("settings.options.after.m2")},
                           {"300", tr("settings.options.after.m5")},
                           {"0", tr("settings.options.never")}}});
        fields.push_back({"theme", tr("settings.fields.theme"), Field::Kind::Choice, c.theme,
                          {{"dark", tr("settings.options.theme.dark")}, {"lcd", tr("settings.options.theme.lcd")}}});
        std::vector<std::pair<std::string, std::string>> languages{{"", tr("app.language.auto")}};
        for (const auto& language : pa::i18n::languages()) languages.emplace_back(language.code, language.native_name);
        fields.push_back({"language", tr("app.language.label"), Field::Kind::Choice, c.language, std::move(languages)});

        Field test{"test", tr("settings.buttons.test"), Field::Kind::Button};
        test.action = [this] { send_test(); };
        fields.push_back(test);
        Field beep{"beep", tr("settings.buttons.beep"), Field::Kind::Button};
        beep.action = [] { pa::play_sound(pa::Sound::Alert); };
        fields.push_back(beep);
        Field tour{"tour", tr("settings.fields.tour"), Field::Kind::Toggle};
        tour.on = c.tour_on_launch;
        fields.push_back(tour);
        Field shortcuts{"shortcuts", tr("settings.buttons.shortcuts"), Field::Kind::Button};
        shortcuts.action = [this] { ctx_.push(make_shortcuts(ctx_)); };
        fields.push_back(shortcuts);
        Field save{"save", tr("settings.buttons.save"), Field::Kind::Button};
        save.divider_above = true;
        save.action = [this] { submit(); };
        fields.push_back(save);
        Field about;
        about.id = "about";
        about.kind = Field::Kind::Note;
        about.value = tr("settings.about.version", {{"version", PA_VERSION}}) + "\n" +
                      (c.demo ? std::string(tr("settings.about.demo"))
                              : tr("settings.about.account", {{"email", c.account_email}})) +
                      "\n" + tr("settings.about.config", {{"path", pa::config_path()}}) + "\n" +
                      tr("settings.about.background", {{"service", "pocketalert-notifyd"}});
        fields.push_back(about);
        form_.set_fields(std::move(fields));
    }

private:
    bool on_shortcut(const Key& key) override {
        if (key.slot() == 1) {
            send_test();
            return true;
        }
        return false;
    }

    bool on_key(const Key& key) override {
        // Leaving keeps the changes: a picked language or theme that silently reverts reads as a bug.
        if (!form_.editing() && (key.esc() || key.slot() == 4)) {
            apply(false);
            return true;
        }
        return FormScreen::on_key(key);
    }

    void submit() override { apply(true); }

    void apply(bool stay) {
        auto& ctx = ctx_;
        auto& c = ctx.config();
        const pa::Config before = c;
        const auto number = [this](const char* id, int fallback) { return pa::parse_int(form_.value(id)).value_or(fallback); };
        c.poll_seconds = number("poll", c.poll_seconds);
        c.sound = form_.toggled("sound");
        c.notify_min_level = number("min_level", c.notify_min_level);
        c.notifier_enabled = form_.toggled("notifier");
        c.notify_poll_seconds = number("notify_poll", c.notify_poll_seconds);
        c.screen_dim_seconds = number("dim", c.screen_dim_seconds);
        c.screen_off_seconds = number("off", c.screen_off_seconds);
        c.theme = form_.value("theme");
        c.language = form_.value("language");
        c.tour_on_launch = form_.toggled("tour");
        const bool changed = c.poll_seconds != before.poll_seconds || c.sound != before.sound ||
                             c.notify_min_level != before.notify_min_level ||
                             c.notifier_enabled != before.notifier_enabled ||
                             c.notify_poll_seconds != before.notify_poll_seconds ||
                             c.screen_dim_seconds != before.screen_dim_seconds ||
                             c.screen_off_seconds != before.screen_off_seconds || c.theme != before.theme ||
                             c.language != before.language || c.tour_on_launch != before.tour_on_launch;
        if (!changed && !stay) {
            close_self();
            return;
        }
        if (changed) {
            ctx.save_config();
            ctx.touch();
        }
        // Both apply_theme() and close_self() destroy this screen, so only locals are used after them.
        if (c.theme != before.theme || c.language != before.language) {
            ctx.apply_theme(stay);
        } else if (!stay) {
            close_self();
        }
        ctx.toast(tr("settings.toast.saved"), ToastKind::Success);
    }

    void send_test() {
        pa::MessageCreate message;
        message.title = tr("settings.testAlert.title");
        message.message = tr("settings.testAlert.message");
        run_once<pa::Result<pa::MessageCreated>>([message](pa::Api& api) { return api.send(message); },
                                            [this](pa::Result<pa::MessageCreated>& result) {
                                                if (!result.ok) {
                                                    ctx_.show_error(result.error, tr("settings.errors.testFailed"));
                                                    return;
                                                }
                                                ctx_.toast(tr("settings.toast.testSent"), ToastKind::Success);
                                                ctx_.poll_now();
                                            });
    }

};

} // namespace

std::string scheduled_help() {
    return std::string(tr("scheduled.title")) + "\n" + tr("scheduled.help.filter") + "\n" + tr("scheduled.help.keys");
}

std::unique_ptr<Screen> make_scheduled(Ctx& ctx) {
    return std::make_unique<ScheduledScreen>(ctx);
}

std::unique_ptr<Screen> make_account(Ctx& ctx) {
    return std::make_unique<AccountScreen>(ctx);
}

std::unique_ptr<Screen> make_settings(Ctx& ctx) {
    return std::make_unique<SettingsScreen>(ctx);
}

} // namespace ui
