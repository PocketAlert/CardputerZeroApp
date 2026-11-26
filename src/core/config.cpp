#include "config.h"

#include "api.h"
#include "i18n.h"
#include "parse.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pa {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// When the launcher starts the UI as root, root adopts the session owner's
// home so the app and the notifier (a user service) share one config.
std::optional<SessionUser> device_user() {
    if (::geteuid() != 0) {
        return std::nullopt;
    }
    return session_user();
}

std::string home() {
    if (const auto user = device_user(); user && !user->home.empty() && env("PA_CARDPUTER_CONFIG_DIR").empty()) {
        return user->home;
    }
    auto value = env("HOME");
    return value.empty() ? "/tmp" : value;
}

// While root works in the session user's home, every file operation runs
// with the user's effective ids: files come out owned by the user, and a
// symlink planted in that home cannot point root at a system file. Only the
// UI thread touches files, so the process-wide switch is not observed by
// another thread mid-operation.
class AsSessionUser {
public:
    AsSessionUser() {
        const auto user = device_user();
        if (!user || user->uid == 0) return;
        const int count = ::getgroups(0, nullptr);
        if (count > 0) {
            groups_.resize(static_cast<size_t>(count));
            if (::getgroups(count, groups_.data()) < 0) groups_.clear();
        }
        const gid_t gid = user->gid;
        if (::setgroups(1, &gid) != 0 || ::setegid(user->gid) != 0) {
            restore_groups();
            return;
        }
        if (::seteuid(user->uid) != 0) {
            (void)::setegid(0);
            restore_groups();
            return;
        }
        switched_ = true;
    }
    ~AsSessionUser() {
        if (!switched_) return;
        const int saved = errno;
        (void)::seteuid(0);
        (void)::setegid(0);
        restore_groups();
        errno = saved;
    }
    AsSessionUser(const AsSessionUser&) = delete;
    AsSessionUser& operator=(const AsSessionUser&) = delete;

private:
    void restore_groups() {
        (void)::setgroups(static_cast<int>(groups_.size()), groups_.data());
    }

    std::vector<gid_t> groups_;
    bool switched_ = false;
};

bool make_dirs(const fs::path& dir) {
    std::error_code ec;
    if (fs::is_directory(dir, ec)) return true;
    const auto parent = dir.parent_path();
    if (!parent.empty() && parent != dir && !make_dirs(parent)) return false;
    return ::mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST;
}

std::string random_suffix() {
    unsigned char bytes[8] = {};
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)::read(fd, bytes, sizeof(bytes));
        ::close(fd);
    }
    char out[sizeof(bytes) * 2 + 1];
    for (size_t i = 0; i < sizeof(bytes); ++i) std::snprintf(out + i * 2, 3, "%02x", bytes[i]);
    return std::string(out) + std::to_string(::getpid());
}

std::optional<std::string> read_file(const std::string& path) {
    AsSessionUser as_user;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

template <typename T>
void read_into(const json& j, const char* key, T& out) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return;
    }
    try {
        out = it->get<T>();
    } catch (...) {
    }
}

} // namespace

std::optional<SessionUser> session_user() {
    std::error_code ec;
    uid_t best = 0;
    for (const auto& entry : fs::directory_iterator("/run/user", ec)) {
        const auto name = entry.path().filename().string();
        char* end = nullptr;
        const auto uid = std::strtoul(name.c_str(), &end, 10);
        if (end && *end == '\0' && uid >= 1000 && uid < 60000 && (best == 0 || uid < best)) {
            best = static_cast<uid_t>(uid);
        }
    }
    const passwd* user = ::getpwuid(best != 0 ? best : 1000);
    if (!user) {
        return std::nullopt;
    }
    return SessionUser{user->pw_uid, user->pw_gid, user->pw_dir ? user->pw_dir : ""};
}

std::string config_dir() {
    if (auto custom = env("PA_CARDPUTER_CONFIG_DIR"); !custom.empty()) {
        return custom;
    }
    if (auto xdg = env("XDG_CONFIG_HOME"); !xdg.empty() && fs::path(xdg).is_absolute()) {
        return (fs::path(xdg) / "pocketalert-cardputer").string();
    }
    return (fs::path(home()) / ".config" / "pocketalert-cardputer").string();
}

std::string config_path() {
    return (fs::path(config_dir()) / "config.json").string();
}

std::string state_path() {
    return (fs::path(config_dir()) / "state.json").string();
}

std::string runtime_dir() {
    // The UI (possibly root) and the notifier (the session user) must agree,
    // so both use the session user's runtime dir rather than their own.
    const uid_t uid = ::geteuid() == 0 ? (session_user() ? session_user()->uid : 0) : ::geteuid();
    std::error_code ec;
    if (uid != 0) {
        const auto run = fs::path("/run/user") / std::to_string(uid);
        if (fs::is_directory(run, ec)) return run.string();
    }
    return config_dir();
}

bool write_file_atomic(const std::string& path, const std::string& content, unsigned mode) {
    AsSessionUser as_user;
    const auto dir = fs::path(path).parent_path();
    if (!make_dirs(dir)) {
        return false;
    }
    const std::string tmp = path + ".tmp." + random_suffix();
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, static_cast<mode_t>(mode));
    if (fd < 0) {
        return false;
    }
    const auto fail = [&] {
        const int saved = errno;
        ::close(fd);
        ::unlink(tmp.c_str());
        errno = saved;
        return false;
    };
    size_t written = 0;
    while (written < content.size()) {
        const auto n = ::write(fd, content.data() + written, content.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return fail();
        }
        written += static_cast<size_t>(n);
    }
    // The umask may have narrowed the mode; fchmod acts on this file, never a link.
    if (::fchmod(fd, static_cast<mode_t>(mode)) != 0 || ::fsync(fd) != 0) {
        return fail();
    }
    if (::close(fd) != 0) {
        const int saved = errno;
        ::unlink(tmp.c_str());
        errno = saved;
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        const int saved = errno;
        ::unlink(tmp.c_str());
        errno = saved;
        return false;
    }
    // Make the rename itself durable across a power cut.
    if (const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dir_fd >= 0) {
        (void)::fsync(dir_fd);
        ::close(dir_fd);
    }
    return true;
}

bool load_config(Config& config, std::string* error) {
    const auto content = read_file(config_path());
    if (!content) {
        if (error) *error = "no config";
        return false;
    }
    const auto j = json::parse(*content, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = "config is not valid JSON";
        return false;
    }
    read_into(j, "api_key", config.api_key);
    read_into(j, "account_email", config.account_email);
    read_into(j, "demo", config.demo);
    read_into(j, "poll_seconds", config.poll_seconds);
    read_into(j, "notify_poll_seconds", config.notify_poll_seconds);
    read_into(j, "sound", config.sound);
    read_into(j, "notifier_enabled", config.notifier_enabled);
    read_into(j, "notify_min_level", config.notify_min_level);
    read_into(j, "theme", config.theme);
    read_into(j, "language", config.language);
    if (!config.language.empty() && !i18n::is_supported(config.language)) config.language.clear();
    read_into(j, "device_tid", config.device_tid);
    read_into(j, "screen_dim_seconds", config.screen_dim_seconds);
    read_into(j, "screen_off_seconds", config.screen_off_seconds);
    read_into(j, "onboarded", config.onboarded);
    read_into(j, "tour_on_launch", config.tour_on_launch);
    if (config.poll_seconds < 5) config.poll_seconds = 5;
    if (config.notify_poll_seconds < 15) config.notify_poll_seconds = 15;
    config.screen_dim_seconds = std::clamp(config.screen_dim_seconds, 0, 3600);
    config.screen_off_seconds = std::clamp(config.screen_off_seconds, 0, 3600);
    return true;
}

bool save_config(const Config& config, std::string* error) {
    const json j{{"api_key", config.api_key},
                 {"account_email", config.account_email},
                 {"demo", config.demo},
                 {"poll_seconds", config.poll_seconds},
                 {"notify_poll_seconds", config.notify_poll_seconds},
                 {"sound", config.sound},
                 {"notifier_enabled", config.notifier_enabled},
                 {"notify_min_level", config.notify_min_level},
                 {"theme", config.theme},
                 {"language", config.language},
                 {"device_tid", config.device_tid},
                 {"screen_dim_seconds", config.screen_dim_seconds},
                 {"screen_off_seconds", config.screen_off_seconds},
                 {"onboarded", config.onboarded},
                 {"tour_on_launch", config.tour_on_launch}};
    const auto text = j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
    if (!write_file_atomic(config_path(), text, 0600)) {
        const int saved = errno;
        if (error) *error = std::string("cannot write ") + config_path() + ": " + std::strerror(saved);
        return false;
    }
    return true;
}

bool load_state(NotifyState& state) {
    const auto content = read_file(state_path());
    if (!content) {
        return false;
    }
    const auto j = json::parse(*content, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }
    read_into(j, "seen_tids", state.seen_tids);
    read_into(j, "unread", state.unread);
    return true;
}

bool save_state(const NotifyState& state) {
    const json j{{"seen_tids", state.seen_tids}, {"unread", state.unread}};
    return write_file_atomic(state_path(), j.dump(-1, ' ', false, json::error_handler_t::replace) + "\n", 0600);
}

namespace {

std::string trim_id(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char c) { return c == '\0' || std::isspace(c); }),
                value.end());
    return value;
}

std::string raw_hardware_serial() {
    if (const auto dt = read_file("/proc/device-tree/serial-number")) {
        if (auto value = trim_id(*dt); !value.empty()) return value;
    }
    if (const auto cpuinfo = read_file("/proc/cpuinfo")) {
        std::istringstream in(*cpuinfo);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("Serial", 0) == 0) {
                if (const auto colon = line.find(':'); colon != std::string::npos) {
                    if (auto value = trim_id(line.substr(colon + 1)); !value.empty()) return value;
                }
            }
        }
    }
    if (const auto machine = read_file("/etc/machine-id")) {
        // machine-id must not leave the box in clear; it is only ever hashed.
        return "machine-id:" + trim_id(*machine);
    }
    return {};
}

} // namespace

std::string hardware_id() {
    const auto raw = raw_hardware_serial();
    if (raw.empty() || raw == "machine-id:") {
        return {};
    }
    // FNV-1a 64: stable, dependency-free, and hides the raw serial.
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : "pocketalert-cardputer:" + raw) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char out[24];
    std::snprintf(out, sizeof(out), "cp0-%016llx", static_cast<unsigned long long>(hash));
    return out;
}

std::optional<std::string> cli_api_key() {
    if (auto key = env("POCKETALERT_API_KEY"); !key.empty()) {
        return key;
    }
    std::string path = env("POCKETALERT_CONFIG");
    if (path.empty()) {
        path = (fs::path(home()) / ".pocketalert" / "config.json").string();
    }
    const auto content = read_file(path);
    if (!content) {
        return std::nullopt;
    }
    const auto j = json::parse(*content, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("api_key") || !j["api_key"].is_string()) {
        return std::nullopt;
    }
    auto key = j["api_key"].get<std::string>();
    if (key.empty()) {
        return std::nullopt;
    }
    return key;
}

namespace {

std::string lock_path() {
    return (fs::path(runtime_dir()) / "pocketalert-cardputer-ui.lock").string();
}

int open_lock_file() {
    const auto path = lock_path();
    AsSessionUser as_user;
    make_dirs(fs::path(path).parent_path());
    return ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
}

} // namespace

// flock, not a pid file: the kernel drops the lock when the UI exits or
// crashes, so there is no stale or forged pid to trust.
UiLock::UiLock() : fd_(open_lock_file()) {
    if (fd_ < 0) return;
    // The notifier may hold a shared lock for a moment while it checks.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) return;
        if (errno != EWOULDBLOCK && errno != EINTR) break;
        ::usleep(10 * 1000);
    }
    ::close(fd_);
    fd_ = -1;
}

UiLock::~UiLock() {
    if (fd_ >= 0) ::close(fd_);
}

bool UiLock::held_by_live_process() {
    const int fd = open_lock_file();
    if (fd < 0) {
        return false;
    }
    const bool free = ::flock(fd, LOCK_SH | LOCK_NB) == 0;
    ::close(fd);
    return !free;
}

} // namespace pa
