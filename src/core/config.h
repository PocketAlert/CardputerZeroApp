#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

namespace pa {

struct Config {
    std::string api_key;
    std::string account_email;
    bool demo = false;
    int poll_seconds = 15;
    int notify_poll_seconds = 60;
    bool sound = true;
    bool notifier_enabled = true;
    // Messages below this level never beep (-2 silent is always quiet).
    int notify_min_level = -1;
    std::string theme = "dark";
    // UI language code; empty follows the system locale.
    std::string language;
    // This Cardputer's entry in the account's device list, once registered.
    std::string device_tid;
    // Idle time before the backlight dims, then turns off; 0 never.
    int screen_dim_seconds = 30;
    int screen_off_seconds = 60;
    // The first-run tour was shown (or skipped).
    bool onboarded = false;
    bool tour_on_launch = false;
};

// Both the UI and the notifier daemon read and write this, so it lives in a
// separate file from Config and is always replaced atomically.
struct NotifyState {
    std::vector<std::string> seen_tids;
    int unread = 0;

    bool operator==(const NotifyState& other) const {
        return unread == other.unread && seen_tids == other.seen_tids;
    }
    bool operator!=(const NotifyState& other) const { return !(*this == other); }
};

std::string config_dir();
std::string config_path();
std::string state_path();
// The session user's /run/user/<uid>, shared by the UI and the notifier.
std::string runtime_dir();

bool load_config(Config& config, std::string* error = nullptr);
bool save_config(const Config& config, std::string* error = nullptr);
bool load_state(NotifyState& state);
bool save_state(const NotifyState& state);

// Owner of the device's user session: the lowest /run/user/<uid> at or above
// 1000. That account runs the launcher, PipeWire and the notifier, whatever
// its name is.
struct SessionUser {
    uid_t uid = 0;
    gid_t gid = 0;
    std::string home;
};
std::optional<SessionUser> session_user();

// Stable per-board id, "cp0-" + 16 hex chars: a hash of the SoC serial, or of
// /etc/machine-id when the board has none. Empty when neither is readable.
std::string hardware_id();

// Key from the CLI (`pocketalert auth`) when it is installed on the same device.
std::optional<std::string> cli_api_key();

// Temp file + fsync + rename; never follows a symlink, and runs as the
// session user when the caller is root.
bool write_file_atomic(const std::string& path, const std::string& content, unsigned mode);

// Held while the UI runs, so the notifier stays quiet.
class UiLock {
public:
    UiLock();
    ~UiLock();
    UiLock(const UiLock&) = delete;
    UiLock& operator=(const UiLock&) = delete;

    static bool held_by_live_process();

private:
    int fd_ = -1;
};

} // namespace pa
