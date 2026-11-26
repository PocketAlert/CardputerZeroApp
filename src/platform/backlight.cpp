#include "backlight.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#if !USE_DESKTOP
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform {
namespace {

[[maybe_unused]] int read_int(const std::filesystem::path& path) {
    std::ifstream in(path);
    int value = -1;
    return in >> value ? value : -1;
}

bool write_int(const std::filesystem::path& path, int value) {
    std::ofstream out(path);
    out << value;
    out.flush();
    return static_cast<bool>(out);
}

} // namespace

Backlight::Backlight(std::string framebuffer) : framebuffer_(std::move(framebuffer)) {
#if !USE_DESKTOP
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/backlight", ec)) {
        const int max = read_int(entry.path() / "max_brightness");
        if (max > 0) {
            dir_ = entry.path().string();
            const int current = read_int(entry.path() / "brightness");
            normal_ = current > 0 ? current : max;
            break;
        }
    }
#endif
}

Backlight::~Backlight() {
    // Never leave the device dark after the app exits.
    if (power_ != ScreenPower::On) set(ScreenPower::On);
}

bool Backlight::write_brightness(int value) {
    if (dir_.empty()) return false;
    return write_int(std::filesystem::path(dir_) / "brightness", value);
}

bool Backlight::blank(bool off) {
#if !USE_DESKTOP
    bool done = false;
    if (!dir_.empty()) {
        const auto bl_power = std::filesystem::path(dir_) / "bl_power";
        std::error_code ec;
        if (std::filesystem::exists(bl_power, ec)) done = write_int(bl_power, off ? 4 : 0);
    }
    if (!framebuffer_.empty()) {
        const int fd = ::open(framebuffer_.c_str(), O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            done = ::ioctl(fd, FBIOBLANK, off ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK) == 0 || done;
            ::close(fd);
        }
    }
    return done;
#else
    (void)off;
    return false;
#endif
}

bool Backlight::set(ScreenPower power) {
    if (power == power_) return true;
    bool ok = false;
    switch (power) {
        case ScreenPower::On: {
            const bool unblanked = power_ == ScreenPower::Off ? blank(false) : false;
            ok = write_brightness(normal_) || unblanked;
            break;
        }
        case ScreenPower::Dim:
            if (power_ == ScreenPower::Off) blank(false);
            ok = write_brightness(std::max(1, normal_ / 4));
            break;
        case ScreenPower::Off: {
            const bool dark = write_brightness(0);
            ok = blank(true) || dark;
            break;
        }
    }
    power_ = power;
    return ok;
}

} // namespace platform
