#pragma once

#include "api.h"
#include "backlight.h"
#include "config.h"
#include "key.h"
#include "widgets.h"
#include "worker.h"

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {
class AssetManager;
}

namespace ui {

class Screen;
class Modal;

enum class ToastKind { Info, Success, Error, Message };

namespace detail {
// A job that throws still answers its caller, with a failed result.
template <typename U>
void mark_failed(pa::Result<U>& result, const std::string& why) {
    result.ok = false;
    result.error.status = -1;
    result.error.message = "Internal error: " + why;
}
inline void mark_failed(pa::HttpResponse& response, const std::string& why) {
    response.status = 0;
    response.transport_error = why;
}
// Aggregates of Results: their members already default to "not ok".
template <typename T>
void mark_failed(T&, const std::string&) {}
} // namespace detail

class Ctx {
public:
    Ctx(app::AssetManager& assets, std::string framebuffer);
    ~Ctx();
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;

    void start();

    pa::Config& config() { return config_; }
    void save_config();
    bool signed_in() const { return config_.demo || !config_.api_key.empty(); }
    void configure_api();
    void sign_in_with_key(const std::string& api_key, const std::string& email);
    void start_demo();
    void sign_out();
    std::shared_ptr<pa::Api> api() const { return api_; }

    // Runs job(api) on the worker thread and done(result) back on the UI thread,
    // unless the owner (a screen's alive token) is gone by then.
    template <typename T>
    void run(std::weak_ptr<bool> owner, std::function<T(pa::Api&)> job, std::function<void(T&)> done) {
        auto api = api_;
        auto post = worker_.ui_poster();
        set_busy_delta(1);
        worker_.post([api, job = std::move(job), done = std::move(done), owner, post, this]() mutable {
            auto result = std::make_shared<T>();
            try {
                *result = job(*api);
            } catch (const std::exception& e) {
                detail::mark_failed(*result, e.what());
            }
            post([result, done = std::move(done), owner, this]() mutable {
                set_busy_delta(-1);
                if (auto alive = owner.lock()) {
                    done(*result);
                }
            });
        });
    }

    void push(std::unique_ptr<Screen> screen);
    // Pops `self` if it is still on top; a late callback of a screen that is
    // already gone, or covered by another one, never pops the wrong screen.
    void pop(const Screen* self);
    void reset(std::unique_ptr<Screen> screen);
    void open_modal(std::unique_ptr<Modal> modal);
    void close_modal(Modal* modal);
    bool has_modal() const { return !modals_.empty(); }

    void toast(const std::string& text, ToastKind kind = ToastKind::Info, int level = 0, const std::string& sub = "");
    void show_error(const pa::ApiError& error, const std::string& prefix = "");

    const std::vector<pa::Application>& apps() const { return apps_; }
    const std::vector<pa::Device>& devices() const { return devices_; }
    void load_lookups(std::function<void()> done = {});
    std::string app_color(const std::string& name) const;
    std::vector<std::pair<std::string, std::string>> app_options(const std::string& none_label) const;
    std::vector<std::pair<std::string, std::string>> device_options() const;

    void poll_now();
    int unread() const { return status_.unread; }

    // Screens deeper in the stack report message edits here, so the inbox can
    // patch its list on resume instead of downloading it again.
    struct MessageChange {
        std::string tid;
        bool removed = false;
        bool is_read = false;
    };
    void note_message_change(MessageChange change);
    // Position just past the newest change; a reader starts its cursor here.
    size_t message_changes_end() const { return changes_base_ + message_changes_.size(); }
    // Appends the changes after `cursor` to `out` and advances it. Returns
    // false when some of them were already dropped: the reader must reload.
    bool take_message_changes(size_t& cursor, std::vector<MessageChange>& out) const;

    void handle_key(uint32_t code, bool long_pressed, bool released, uint32_t repeat);
    // Applies queued results and navigation; true when it did any work.
    bool frame();
    int wake_fd() const { return worker_.wake_fd(); }
    bool quit_requested() const { return quit_; }
    void request_quit() { quit_ = true; }

    app::AssetManager& assets() { return assets_; }
    Screen* current() const { return stack_.empty() ? nullptr : stack_.back().get(); }
    void refresh_chrome(Screen* screen);
    // Rebuilds every screen with the current theme and language, which destroys the caller's screen.
    void apply_theme(bool reopen_settings = true);
    void load_language();
    // Restarts the idle countdown after a settings change.
    void touch() { wake(); }

private:
    struct NavOp {
        enum class Kind { Push, Pop, Reset } kind;
        std::unique_ptr<Screen> screen;
        const Screen* expected = nullptr;
    };

    void apply_nav();
    void drop_modals_of(const Screen* screen);
    void set_busy_delta(int delta);
    void tick();
    void poll();
    uint32_t poll_interval_ms() const;
    void update_status();
    void register_device();
    void offer_tour();
    void dispatch(const Key& key);
    void show_help();
    void take_screenshot();
    void hide_toast();
    void wake();
    void set_power(platform::ScreenPower power);
    static void tick_cb(lv_timer_t* timer);
    static void toast_timer_cb(lv_timer_t* timer);

    app::AssetManager& assets_;
    pa::Config config_;
    std::shared_ptr<pa::Api> api_;
    // Bumped whenever the account changes, so late results of the old one are dropped.
    uint64_t api_generation_ = 0;
    Worker worker_;
    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<NavOp> nav_ops_;
    std::vector<std::unique_ptr<Modal>> modals_;
    std::vector<std::unique_ptr<Modal>> closed_modals_;
    std::vector<pa::Application> apps_;
    std::vector<pa::Device> devices_;
    std::vector<MessageChange> message_changes_;
    size_t changes_base_ = 0;
    bool registering_device_ = false;
    uint32_t device_attempt_ms_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    StatusInfo status_;
    int busy_ = 0;
    bool polling_ = false;
    int poll_failures_ = 0;
    uint32_t last_poll_ms_ = 0;
    uint32_t last_device_status_ms_ = 0;
    bool quit_ = false;
    bool esc_long_fired_ = false;
    lv_timer_t* tick_timer_ = nullptr;
    lv_obj_t* toast_ = nullptr;
    lv_timer_t* toast_timer_ = nullptr;

    platform::Backlight backlight_;
    platform::ScreenPower power_ = platform::ScreenPower::On;
    // Software stand-in when the backlight cannot be driven (no access, simulator).
    lv_obj_t* shade_ = nullptr;
    uint32_t last_input_ms_ = 0;
    // A key that woke the screen is consumed up to its release.
    uint32_t swallowed_key_ = 0;

    std::unique_ptr<pa::UiLock> ui_lock_;
};

} // namespace ui
