#pragma once

#include "linux_input.h"
#include "lvgl.h"

#include <cstdint>
#include <string>

namespace ui {

struct Key {
    uint32_t code = 0;
    std::string text;

    bool is(uint32_t c) const { return code == c; }
    bool is_char(char c) const { return text.size() == 1 && text[0] == c; }
    bool printable() const { return !text.empty(); }
    bool enter() const { return code == LV_KEY_ENTER; }
    bool esc() const { return code == LV_KEY_ESC; }
    bool backspace() const { return code == LV_KEY_BACKSPACE; }
    bool tab() const { return code == LV_KEY_NEXT; }
    bool shift_tab() const { return code == LV_KEY_PREV; }

    // F/Z/X/C double as arrows outside text entry, per the CardputerZero key guidelines.
    bool up() const { return code == LV_KEY_UP || is_char('f') || is_char(';'); }
    bool down() const { return code == LV_KEY_DOWN || is_char('x') || is_char('.'); }
    bool left() const { return code == LV_KEY_LEFT || is_char('z') || is_char(','); }
    bool right() const { return code == LV_KEY_RIGHT || is_char('c') || is_char('/'); }
    bool page_up() const { return code == platform::kKeyPageUp; }
    bool page_down() const { return code == platform::kKeyPageDown; }

    // Number keys 4-8 sit right under the screen and drive the bottom bar.
    int slot() const {
        if (text.size() == 1 && text[0] >= '4' && text[0] <= '8') {
            return text[0] - '4';
        }
        return -1;
    }
};

inline Key make_key(uint32_t code) {
    return Key{code, platform::key_text(code)};
}

} // namespace ui
