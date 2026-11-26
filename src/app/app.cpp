#include "app.h"

#include "asset_manager.h"
#include "ctx.h"
#include "http.h"
#include "linux_input.h"
#include "logger.h"
#include "notify.h"
#include "theme.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"

#include <sstream>
#include <vector>
#else
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE ""
#endif
#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif
#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

namespace app {
namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

void on_key(uint32_t key, bool long_pressed, bool released, uint32_t repeat, void* user_data) {
    static_cast<ui::Ctx*>(user_data)->handle_key(key, long_pressed, released, repeat);
}

#if !USE_DESKTOP
// The LCD is not always fb0 (HDMI may take it), so find the ST7789 by driver name.
std::string framebuffer_device() {
    if (const char* env = std::getenv("PA_FRAMEBUFFER"); env && env[0]) {
        return env;
    }
    if (APP_FRAMEBUFFER_DEVICE[0] != '\0') {
        return APP_FRAMEBUFFER_DEVICE;
    }
    std::ifstream proc("/proc/fb");
    int index = -1;
    std::string name;
    while (proc >> index >> name) {
        if (name.find("st7789") != std::string::npos) {
            return "/dev/fb" + std::to_string(index);
        }
    }
    return "/dev/fb0";
}

lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display || lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        return nullptr;
    }
#else
    auto* display = lv_linux_fbdev_create();
    const auto device = framebuffer_device();
    LOG_INFO("framebuffer: {}", device);
    if (!display || lv_linux_fbdev_set_file(display, device.c_str()) != LV_RESULT_OK) {
        return nullptr;
    }
#endif
    platform::init_key_input(display);
    return display;
}
#endif

#if USE_DESKTOP
// PA_SCRIPT drives the simulator for screenshots and smoke tests, e.g.
// "w1500 s:inbox down enter w800 s:message esc 8 s:menu". Steps: wN waits N ms,
// s:NAME saves PA_SCRIPT_DIR/NAME.png, t:TEXT types, k:C presses one key, and
// enter/esc/up/down/left/right/tab/bs/pgdn/pgup press that key (+key holds it,
// -key releases). It quits at the end.
struct Script {
    std::vector<std::string> steps;
    size_t index = 0;
    uint32_t resume_at = 0;
    DesktopSimulatorFrame* frame = nullptr;
    std::string dir;
};

void press(Script& script, uint32_t key) {
    script.frame->inject_key(key, true);
    script.frame->inject_key(key, false);
}

void script_step(lv_timer_t* timer) {
    auto& script = *static_cast<Script*>(lv_timer_get_user_data(timer));
    if (lv_tick_get() < script.resume_at) return;
    if (script.index >= script.steps.size()) {
        g_stop = 1;
        return;
    }
    const auto step = script.steps[script.index++];
    script.resume_at = lv_tick_get() + 250;
    static const std::vector<std::pair<std::string, uint32_t>> named = {
        {"enter", LV_KEY_ENTER}, {"esc", LV_KEY_ESC},        {"up", LV_KEY_UP},
        {"down", LV_KEY_DOWN},   {"left", LV_KEY_LEFT},      {"right", LV_KEY_RIGHT},
        {"tab", LV_KEY_NEXT},    {"bs", LV_KEY_BACKSPACE},   {"pgdn", platform::kKeyPageDown},
        {"pgup", platform::kKeyPageUp}, {"help", platform::kKeyHelp},
    };
    // "+down" holds a key until "-down", to exercise autorepeat.
    const bool hold = step[0] == '+' || step[0] == '-';
    const auto key_name = hold ? step.substr(1) : step;
    for (const auto& [name, key] : named) {
        if (key_name == name) {
            if (hold) {
                script.frame->inject_key(key, step[0] == '+');
            } else {
                press(script, key);
            }
            return;
        }
    }
    if (step[0] == 'w') {
        script.resume_at = lv_tick_get() + static_cast<uint32_t>(std::atoi(step.c_str() + 1));
    } else if (step.rfind("s:", 0) == 0) {
        const auto path = script.dir + "/" + step.substr(2) + ".png";
        LOG_INFO("script screenshot {}: {}", path, script.frame->save_png(path.c_str()) ? "ok" : "failed");
    } else if (step.rfind("t:", 0) == 0) {
        for (char c : step.substr(2)) press(script, c == '_' ? ' ' : static_cast<uint32_t>(c));
    } else if (step.rfind("k:", 0) == 0 && step.size() > 2) {
        press(script, static_cast<uint32_t>(step[2]));
    } else if (step.size() == 1) {
        press(script, static_cast<uint32_t>(step[0]));
    }
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("pocketalert");

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    pa::http_global_init();
    lv_init();
#if !USE_DESKTOP
    // LVGL's default delay is a busy loop; anything that waits must sleep.
    lv_delay_set_cb([](uint32_t ms) { ::usleep(ms * 1000U); });
#endif

    AssetManager assets;
    if (const auto audio = assets.resolve("audio"); !audio.empty()) {
        pa::set_sounds_dir(audio.string());
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
    const std::string framebuffer;
#else
    auto* display = init_device_display();
    const std::string framebuffer = APP_USE_DRM ? std::string() : framebuffer_device();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    ui::load_fonts(assets);
    ui::Ctx ctx(assets, framebuffer);
    ui::apply_to_display(display);

#if USE_DESKTOP
    lv_subject_t dark_shell;
    lv_subject_init_int(&dark_shell, ui::theme().name == "lcd" ? 0 : 1);
    simulator_frame.bind_dark_mode(&dark_shell);
#endif

    // The simulator keypad installs a default group; objects must not join it,
    // or LVGL would also feed keys into textareas behind our router's back.
    lv_group_set_default(nullptr);
    platform::set_key_handler(on_key, &ctx);
    ctx.start();

#if USE_DESKTOP
    Script script;
    lv_timer_t* script_timer = nullptr;
    if (const char* steps = std::getenv("PA_SCRIPT"); steps && steps[0]) {
        std::istringstream in(steps);
        for (std::string step; in >> step;) script.steps.push_back(step);
        script.frame = &simulator_frame;
        const char* dir = std::getenv("PA_SCRIPT_DIR");
        script.dir = dir ? dir : ".";
        script.resume_at = lv_tick_get() + 600;
        script_timer = lv_timer_create(script_step, 50, &script);
    }
#endif

    LOG_INFO("Pocket Alert started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    // Event-driven: sleep until a key, a finished network job or the next
    // LVGL timer. An idle app wakes about once a second (status tick), not
    // two hundred times.
    while (!g_stop && !ctx.quit_requested()
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        const uint32_t idle = lv_timer_handler();
        if (ctx.frame()) {
            continue;
        }
        platform::wait_for_events(std::min<uint32_t>(idle, 1000U), ctx.wake_fd());
    }

#if USE_DESKTOP
    if (script_timer) lv_timer_delete(script_timer);
#endif
    platform::set_key_handler(nullptr, nullptr);
    return 0;
}

} // namespace app
