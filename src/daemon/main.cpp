// Background notifier: polls the inbox while the UI is closed and beeps on
// new alerts. It shares config.json and state.json with the UI, and stays
// quiet while the UI runs (the UI polls and alerts on its own).

#include "api.h"
#include "config.h"
#include "http.h"
#include "notify.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <pthread.h>
#include <thread>

namespace {

bool g_stop = false;
bool g_verbose = false;
sigset_t g_signals;

void log(const char* format, const std::string& text = "") {
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
    std::fprintf(stdout, "[%s] ", stamp);
    std::fprintf(stdout, format, text.c_str());
    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}

// One wakeup per check: SIGTERM/SIGINT are blocked and collected here, so
// the process neither polls a flag nor misses a stop request.
void sleep_seconds(int seconds) {
#if defined(__linux__)
    timespec timeout{seconds, 0};
    for (;;) {
        const int sig = sigtimedwait(&g_signals, nullptr, &timeout);
        if (sig > 0) {
            g_stop = true;
            return;
        }
        if (errno != EINTR) return;
    }
#else
    // macOS has no sigtimedwait; this path only serves local development.
    for (int i = 0; i < seconds && !g_stop; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        sigset_t pending;
        sigpending(&pending);
        if (sigismember(&pending, SIGTERM) || sigismember(&pending, SIGINT)) g_stop = true;
    }
#endif
}

std::string sounds_dir() {
    if (const char* env = std::getenv("PA_SOUNDS_DIR"); env && env[0]) return env;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path installed = fs::path("/usr/share") / APP_NAME / "audio";
    if (fs::exists(installed, ec)) return installed.string();
    return (fs::path(APP_SOURCE_ASSETS_ROOT) / "audio").string();
}

// Returns seconds to wait before the next check. Logs only transitions, not
// every failed attempt, so a long outage leaves a few lines in the journal.
int check_once(const pa::Config& config, int& failures) {
    const int interval = std::max(15, config.notify_poll_seconds);
    if (config.demo || config.api_key.empty()) {
        if (g_verbose) log("not signed in, idle");
        return 60;
    }
    if (!config.notifier_enabled) {
        if (g_verbose) log("disabled in settings, idle");
        return 60;
    }
    if (pa::UiLock::held_by_live_process()) {
        if (g_verbose) log("app is open, letting it notify");
        return std::min(interval, 30);
    }

    pa::Credentials credentials;
    credentials.api_key = config.api_key;
    pa::Api api(std::make_shared<pa::CurlTransport>(std::string("pocketalert-cardputer-notifyd/") + PA_VERSION), pa::kDefaultBaseUrl,
                credentials);
    pa::MessageQuery query;
    query.size = 20;
    const auto result = api.messages(query);
    if (!result.ok) {
        ++failures;
        // Back off up to 10 minutes; a revoked key gets the long wait straight away.
        const int backoff = result.error.status == 401 ? 600 : std::min(600, interval * (1 << std::min(failures, 4)));
        if (failures == 1 || g_verbose) log("check failed: %s", result.error.text());
        return backoff;
    }
    if (failures > 0) log("connection restored");
    failures = 0;

    pa::NotifyState state;
    pa::load_state(state);
    const auto before = state;
    const auto detection = pa::detect_new(result.value.data, state);
    // The state file sits on the SD card: rewrite it only when it changed.
    if (state != before) pa::save_state(state);

    if (!detection.fresh.empty()) {
        // Titles stay out of the journal unless asked for: they may be private.
        log("%s new message(s)", std::to_string(detection.fresh.size()));
        if (g_verbose) {
            for (const auto& message : detection.fresh) log("new: %s", message.title);
        }
        const int loudest = pa::loudest_level(detection.fresh);
        if (config.sound && loudest > -2 && loudest >= config.notify_min_level) {
            pa::play_sound(loudest >= 2 ? pa::Sound::Critical : pa::Sound::Alert);
        }
    } else if (g_verbose) {
        log("no new messages (%s unread)", std::to_string(detection.unread));
    }
    return interval;
}

} // namespace

int main(int argc, char** argv) {
    bool once = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--once") == 0) once = true;
        else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) g_verbose = true;
        else if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("pocketalert-cardputer-notifyd %s\n", PA_VERSION);
            return 0;
        } else {
            std::fprintf(stderr, "usage: pocketalert-cardputer-notifyd [--once] [--verbose]\n");
            return 2;
        }
    }

    sigemptyset(&g_signals);
    sigaddset(&g_signals, SIGINT);
    sigaddset(&g_signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &g_signals, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
    pa::http_global_init();
    pa::set_sounds_dir(sounds_dir());
    log("started, config %s", pa::config_path());

    int failures = 0;
    while (!g_stop) {
        pa::Config config;
        pa::load_config(config);
        const int wait = check_once(config, failures);
        if (once) break;
        sleep_seconds(wait);
    }
    // Let a just-started player detach before the process exits under systemd.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
