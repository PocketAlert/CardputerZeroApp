#include "notify.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <fcntl.h>
#include <grp.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pa {
namespace {

std::mutex sounds_mutex;
std::string sounds_dir;

const char* file_for(Sound sound) {
    switch (sound) {
        case Sound::Alert: return "alert.wav";
        case Sound::Critical: return "critical.wav";
        case Sound::Click: return "click.wav";
        case Sound::Sent: return "sent.wav";
    }
    return "alert.wav";
}

struct Launch {
    std::vector<std::string> argv;
    std::vector<std::string> env;
    uid_t uid = 0;
    gid_t gid = 0;
    bool drop_privileges = false;
    int max_fd = 1024;
};

// Everything is prepared before fork(): in a threaded process the child may
// only make async-signal-safe calls (no allocation, no PATH lookup).
void spawn_detached(const Launch& launch) {
    std::vector<char*> args;
    for (const auto& arg : launch.argv) args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    std::vector<char*> envp;
    for (const auto& var : launch.env) envp.push_back(const_cast<char*>(var.c_str()));
    envp.push_back(nullptr);

    const pid_t child = fork();
    if (child < 0) {
        return;
    }
    if (child == 0) {
        // Double fork so the player is reparented to init and never becomes a zombie.
        if (fork() != 0) {
            _exit(0);
        }
        const int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        // The player must not inherit the framebuffer, input or lock fds, nor
        // the notifier's blocked SIGTERM (it would then ignore systemd).
        for (int fd = STDERR_FILENO + 1; fd < launch.max_fd; ++fd) {
            close(fd);
        }
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        // PipeWire only accepts clients of the session's own user. Groups go
        // first: setuid would leave root's supplementary groups in place.
        if (launch.drop_privileges &&
            (setgroups(1, &launch.gid) != 0 || setgid(launch.gid) != 0 || setuid(launch.uid) != 0)) {
            _exit(126);
        }
        execve(args[0], args.data(), envp.data());
        _exit(127);
    }
    waitpid(child, nullptr, 0);
}

#if !defined(__APPLE__)
bool executable(const char* path) {
    return ::access(path, X_OK) == 0;
}
#endif

// The CardputerZero routes audio through PipeWire in the user session (the
// launcher plays the same way). pw-play talks to it directly; paplay and
// aplay reach it through the pulse and ALSA plugins when those are installed.
std::vector<std::string> player_for(const std::string& path) {
#if defined(__APPLE__)
    return {"/usr/bin/afplay", path};
#else
    if (executable("/usr/bin/pw-play")) return {"/usr/bin/pw-play", path};
    if (executable("/usr/bin/paplay")) return {"/usr/bin/paplay", path};
    if (executable("/usr/bin/aplay")) return {"/usr/bin/aplay", "-q", path};
    return {};
#endif
}

// A minimal environment: nothing of root's (LD_*, HOME=/root) reaches a
// player that runs as the session user.
std::vector<std::string> child_environment(const std::string& runtime_dir, const std::string& home) {
    std::vector<std::string> env{"PATH=/usr/bin:/bin"};
    if (!home.empty()) env.push_back("HOME=" + home);
    if (!runtime_dir.empty()) env.push_back("XDG_RUNTIME_DIR=" + runtime_dir);
    if (const char* lang = std::getenv("LANG"); lang && lang[0]) {
        env.push_back(std::string("LANG=") + lang);
    }
    return env;
}

} // namespace

Detection detect_new(const std::vector<Message>& latest, NotifyState& state) {
    Detection detection;
    const bool first_run = state.seen_tids.empty();
    std::unordered_set<std::string> seen(state.seen_tids.begin(), state.seen_tids.end());

    std::vector<std::string> added;
    for (const auto& message : latest) {
        if (!message.is_read) {
            ++detection.unread;
        }
        if (seen.count(message.tid)) {
            continue;
        }
        added.push_back(message.tid);
        if (!first_run && !message.is_read) {
            detection.fresh.push_back(message);
        }
    }

    if (!added.empty()) {
        state.seen_tids.insert(state.seen_tids.begin(), added.begin(), added.end());
        if (state.seen_tids.size() > kSeenHistory) {
            state.seen_tids.resize(kSeenHistory);
        }
    }
    state.unread = detection.unread;
    return detection;
}

int loudest_level(const std::vector<Message>& messages) {
    int level = -2;
    for (const auto& message : messages) {
        level = std::max(level, message.priority);
    }
    return level;
}

void set_sounds_dir(std::string dir) {
    std::lock_guard<std::mutex> lock(sounds_mutex);
    sounds_dir = std::move(dir);
}

void play_sound(Sound sound) {
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(sounds_mutex);
        dir = sounds_dir;
    }
    if (dir.empty()) {
        return;
    }
    const auto path = (std::filesystem::path(dir) / file_for(sound)).string();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }
    Launch launch;
    launch.argv = player_for(path);
    if (launch.argv.empty()) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[pocketalert] no audio player found (pw-play, paplay or aplay)\n");
        }
        return;
    }

    const char* current_runtime = std::getenv("XDG_RUNTIME_DIR");
    const char* current_home = std::getenv("HOME");
    std::string runtime = current_runtime ? current_runtime : "";
    std::string home = current_home ? current_home : "";
#if !defined(__APPLE__)
    // Without the session's runtime dir the player cannot find PipeWire and
    // plays into nothing: the case when the launcher starts the app as root.
    if (::geteuid() == 0 || runtime.empty()) {
        if (const auto owner = session_user()) {
            runtime = "/run/user/" + std::to_string(owner->uid);
            if (::geteuid() == 0) {
                launch.drop_privileges = true;
                launch.uid = owner->uid;
                launch.gid = owner->gid;
                home = owner->home;
            }
        }
    }
#endif
    if (const long limit = ::sysconf(_SC_OPEN_MAX); limit > 0) {
        launch.max_fd = static_cast<int>(std::min(limit, 4096L));
    }
    launch.env = child_environment(runtime, home);
    spawn_detached(launch);
}

} // namespace pa
