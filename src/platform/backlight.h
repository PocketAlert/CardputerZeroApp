#pragma once

#include <string>

namespace platform {

enum class ScreenPower { On, Dim, Off };

// Drives the LCD backlight through /sys/class/backlight and blanks the
// framebuffer when off. Both need write access (the launcher runs apps as
// root); without it set() reports false and the caller dims in software.
class Backlight {
public:
    explicit Backlight(std::string framebuffer = {});
    Backlight(const Backlight&) = delete;
    Backlight& operator=(const Backlight&) = delete;
    ~Backlight();

    bool set(ScreenPower power);
    ScreenPower power() const { return power_; }

private:
    bool write_brightness(int value);
    bool blank(bool off);

    std::string dir_;
    std::string framebuffer_;
    int normal_ = 0;
    ScreenPower power_ = ScreenPower::On;
};

} // namespace platform
