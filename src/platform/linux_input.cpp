#include "linux_input.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#if !USE_DESKTOP
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <map>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {
namespace {

constexpr uint32_t kLongPressMs = 700;
// Repeats are generated here rather than taken from the kernel: the keypad
// driver may not autorepeat at all, and SDL repeats at its own fixed rate.
constexpr uint32_t kRepeatDelayMs = 320;
constexpr uint32_t kRepeatFirstIntervalMs = 110;
constexpr uint32_t kRepeatMinIntervalMs = 35;

KeyHandler key_handler = nullptr;
void* key_handler_user_data = nullptr;

uint32_t pressed_key = 0;
uint32_t press_started_at = 0;
bool long_press_sent = false;
bool last_key_pressed = false;
uint32_t repeat_count = 0;
uint32_t repeat_interval = kRepeatFirstIntervalMs;
uint32_t next_repeat_at = kRepeatDelayMs;
lv_timer_t* long_press_timer = nullptr;

void emit(uint32_t key, bool long_pressed, bool released, uint32_t repeat = 0) {
    if (key_handler) {
        key_handler(key, long_pressed, released, repeat, key_handler_user_data);
    }
}

void long_press_timer_cb(lv_timer_t*) {
    if (!pressed_key) {
        return;
    }
    const uint32_t held = lv_tick_elaps(press_started_at);
    if (!long_press_sent && held >= kLongPressMs) {
        long_press_sent = true;
        emit(pressed_key, true, false);
    }
    if (pressed_key && held >= next_repeat_at) {
        emit(pressed_key, false, false, ++repeat_count);
        repeat_interval = std::max(kRepeatMinIntervalMs, repeat_interval * 85 / 100);
        next_repeat_at = held + repeat_interval;
    }
}

void route_key_state(uint32_t key, bool pressed) {
    if (pressed && (!last_key_pressed || pressed_key != key)) {
        pressed_key = key;
        press_started_at = lv_tick_get();
        long_press_sent = false;
        repeat_count = 0;
        repeat_interval = kRepeatFirstIntervalMs;
        next_repeat_at = kRepeatDelayMs;
        if (!long_press_timer) {
            long_press_timer = lv_timer_create(long_press_timer_cb, 15, nullptr);
        }
        lv_timer_resume(long_press_timer);
        lv_timer_reset(long_press_timer);
        emit(key, false, false);
    } else if (!pressed && last_key_pressed && pressed_key == key) {
        pressed_key = 0;
        if (long_press_timer) {
            lv_timer_pause(long_press_timer);
        }
        emit(key, false, true);
    }
    last_key_pressed = pressed;
}

#if !USE_DESKTOP

struct EvdevKeypad {
    int fd{-1};
    lv_indev_t* indev{nullptr};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
    bool pending{false};
    // The last read stopped at a key event; more may be queued behind it.
    bool more{false};
    bool left_shift{false};
    bool right_shift{false};
};

// Read in event mode: the main loop sleeps in poll() on these descriptors
// instead of LVGL waking every 16 ms to look for keys.
std::vector<EvdevKeypad*>& keypads() {
    static std::vector<EvdevKeypad*> all;
    return all;
}

// The TCA8418 driver reports sym-layer characters as raw keycodes that only
// mean something through this keymap; the built-in copy is the stock file.
std::map<uint16_t, uint32_t>& symbol_keymap() {
    static std::map<uint16_t, uint32_t> map = [] {
        std::map<uint16_t, uint32_t> built_in = {
            {26, '!'}, {27, '@'}, {39, '#'},  {40, '$'}, {41, '%'}, {43, '^'}, {51, '&'},  {52, '*'},
            {53, '('}, {94, ')'}, {55, '~'},  {69, '`'}, {70, '_'}, {71, '-'}, {72, '+'},  {73, '='},
            {74, '['}, {75, ']'}, {76, '{'},  {77, '}'}, {79, ';'}, {80, ':'}, {81, '\''}, {82, '"'},
            {83, '<'}, {85, '>'}, {86, '\\'}, {89, '|'}, {90, ','}, {91, '.'}, {92, '/'},  {93, '?'},
        };
        static const std::map<std::string, uint32_t> names = {
            {"exclam", '!'},      {"at", '@'},           {"numbersign", '#'}, {"dollar", '$'},
            {"percent", '%'},     {"asciicircum", '^'},  {"ampersand", '&'},  {"asterisk", '*'},
            {"parenleft", '('},   {"parenright", ')'},   {"asciitilde", '~'}, {"grave", '`'},
            {"underscore", '_'},  {"minus", '-'},        {"plus", '+'},       {"equal", '='},
            {"bracketleft", '['}, {"bracketright", ']'}, {"braceleft", '{'},  {"braceright", '}'},
            {"semicolon", ';'},   {"colon", ':'},        {"apostrophe", '\''}, {"quotedbl", '"'},
            {"less", '<'},        {"greater", '>'},      {"backslash", '\\'}, {"bar", '|'},
            {"comma", ','},       {"period", '.'},       {"slash", '/'},      {"question", '?'},
        };
        const char* path = std::getenv("PA_KEYMAP_PATH");
        std::ifstream in(path ? path : "/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map");
        if (!in) {
            return built_in;
        }
        std::map<uint16_t, uint32_t> loaded;
        std::string line;
        while (std::getline(in, line)) {
            if (const auto hash = line.find('#'); hash != std::string::npos) {
                line.resize(hash);
            }
            std::istringstream parser(line);
            std::string keyword, symbol;
            unsigned code = 0;
            char equals = 0;
            if (!(parser >> keyword >> code >> equals >> symbol) || keyword != "keycode" || equals != '=') {
                continue;
            }
            if (const auto it = names.find(symbol); it != names.end()) {
                loaded[static_cast<uint16_t>(code)] = it->second;
            }
        }
        return loaded.empty() ? built_in : loaded;
    }();
    return map;
}

uint32_t map_evdev_key(uint16_t code, bool shifted) {
    if (const auto it = symbol_keymap().find(code); it != symbol_keymap().end()) {
        return it->second;
    }
    static const char letters[] = "qwertyuiop";
    static const char home_row[] = "asdfghjkl";
    static const char bottom_row[] = "zxcvbnm";
    auto letter = [&](char c) -> uint32_t { return shifted ? static_cast<uint32_t>(c - 'a' + 'A') : c; };
    if (code >= KEY_Q && code <= KEY_P) return letter(letters[code - KEY_Q]);
    if (code >= KEY_A && code <= KEY_L) return letter(home_row[code - KEY_A]);
    if (code >= KEY_Z && code <= KEY_M) return letter(bottom_row[code - KEY_Z]);
    if (code >= KEY_1 && code <= KEY_9) return shifted ? static_cast<uint32_t>("!@#$%^&*("[code - KEY_1]) : '1' + (code - KEY_1);
    switch (code) {
        case KEY_0: return shifted ? ')' : '0';
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_ENTER:
        case KEY_KPENTER: return LV_KEY_ENTER;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_DELETE: return LV_KEY_DEL;
        case KEY_TAB: return shifted ? LV_KEY_PREV : LV_KEY_NEXT;
        case KEY_SPACE: return ' ';
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_HOME: return LV_KEY_HOME;
        case KEY_END: return LV_KEY_END;
        case KEY_PAGEUP: return kKeyPageUp;
        case KEY_PAGEDOWN: return kKeyPageDown;
        case KEY_SYSRQ: return kKeyPrintScreen;
        case KEY_HELP: return kKeyHelp;
        case KEY_MINUS: return shifted ? '_' : '-';
        case KEY_EQUAL: return shifted ? '+' : '=';
        case KEY_LEFTBRACE: return shifted ? '{' : '[';
        case KEY_RIGHTBRACE: return shifted ? '}' : ']';
        case KEY_BACKSLASH: return shifted ? '|' : '\\';
        case KEY_SEMICOLON: return shifted ? ':' : ';';
        case KEY_APOSTROPHE: return shifted ? '"' : '\'';
        case KEY_GRAVE: return shifted ? '~' : '`';
        case KEY_COMMA: return shifted ? '<' : ',';
        case KEY_DOT: return shifted ? '>' : '.';
        case KEY_SLASH: return shifted ? '?' : '/';
        default: return 0;
    }
}

void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    keypad->more = false;
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type != EV_KEY) {
            continue;
        }
        if (input.code == KEY_LEFTSHIFT || input.code == KEY_RIGHTSHIFT) {
            (input.code == KEY_LEFTSHIFT ? keypad->left_shift : keypad->right_shift) = input.value != 0;
            continue;
        }
        const bool shifted = keypad->left_shift || keypad->right_shift;
        const auto key = map_evdev_key(input.code, shifted);
        // Kernel autorepeat (value 2) is dropped: holding is timed in route_key_state.
        if (!key || input.value == 2) {
            continue;
        }
        keypad->key = key;
        keypad->state = input.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        keypad->pending = true;
        keypad->more = true;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

void evdev_key_event_cb(lv_event_t*) {
    auto* indev = lv_indev_active();
    auto* keypad = indev ? static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev)) : nullptr;
    // LVGL emits LV_EVENT_KEY on every poll, even when evdev had nothing new.
    if (!keypad || !keypad->pending) {
        return;
    }
    keypad->pending = false;
    route_key_state(keypad->key, keypad->state == LV_INDEV_STATE_PRESSED);
}

void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (keypad) {
        auto& all = keypads();
        all.erase(std::remove(all.begin(), all.end(), keypad), all.end());
        if (keypad->fd >= 0) close(keypad->fd);
        delete keypad;
    }
}

bool has_keyboard_keys(int fd) {
    unsigned long bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
        return false;
    }
    auto has = [&](int code) {
        const int per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (bits[code / per_word] & (1UL << (code % per_word))) != 0;
    };
    return has(KEY_ESC) || has(KEY_ENTER) || has(KEY_A) || has(KEY_4);
}

bool open_keypad(const char* path, lv_display_t* display, bool require_keys) {
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    if (require_keys && !has_keyboard_keys(fd)) {
        close(fd);
        return false;
    }
    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;
    auto* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    lv_indev_add_event_cb(indev, evdev_key_event_cb, LV_EVENT_KEY, nullptr);
    lv_indev_set_display(indev, display);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    keypad->indev = indev;
    keypads().push_back(keypad);
    LV_LOG_USER("keyboard input: %s", path);
    return true;
}

#endif

void desktop_key_event_cb(lv_event_t*) {
    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }
    route_key_state(lv_indev_get_key(indev), lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
}

} // namespace

void init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    const char* configured = std::getenv("PA_KEYBOARD_DEVICE");
    if (!configured || !configured[0]) configured = APP_KEY_INPUT_DEVICE;
    if (configured[0] != '\0') {
        open_keypad(configured, display, false);
        return;
    }
    if (open_keypad("/dev/input/by-path/platform-3f804000.i2c-event", display, false)) {
        return;
    }
    char path[32];
    for (int index = 0; index < 32; ++index) {
        std::snprintf(path, sizeof(path), "/dev/input/event%d", index);
        open_keypad(path, display, true);
    }
#else
    LV_UNUSED(display);
#endif
}

void wait_for_events(uint32_t timeout_ms, int wake_fd) {
#if !USE_DESKTOP
    std::vector<pollfd> fds;
    for (const auto* keypad : keypads()) fds.push_back({keypad->fd, POLLIN, 0});
    if (wake_fd >= 0) fds.push_back({wake_fd, POLLIN, 0});
    const int timeout = timeout_ms > 60000U ? 60000 : static_cast<int>(timeout_ms);
    if (poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout) <= 0) {
        return;
    }
    // Copy: a key handler may not delete an indev, but keep the loop safe anyway.
    const auto ready = keypads();
    for (size_t i = 0; i < ready.size() && i < fds.size(); ++i) {
        if (!(fds[i].revents & POLLIN)) continue;
        // One lv_indev_read hands LVGL a single key change; drain the batch.
        for (int guard = 0; guard < 64; ++guard) {
            lv_indev_read(ready[i]->indev);
            if (!ready[i]->more) break;
        }
    }
#else
    LV_UNUSED(wake_fd);
    // SDL delivers input through its own event queue; a short nap keeps the
    // simulator responsive without spinning.
    lv_delay_ms(timeout_ms < 5U ? timeout_ms : 5U);
#endif
}

void attach_key_router(lv_indev_t* indev) {
    if (indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
        lv_indev_add_event_cb(indev, desktop_key_event_cb, LV_EVENT_KEY, nullptr);
    }
}

void set_key_handler(KeyHandler handler, void* user_data) {
    key_handler = handler;
    key_handler_user_data = user_data;
}

bool is_special_key(uint32_t key) {
    return (key & 0xFFFF00U) == kSpecialKeyBase;
}

std::string key_text(uint32_t key) {
    if (is_special_key(key)) {
        return {};
    }
    if (key >= 0x20 && key < 0x7f) {
        return std::string(1, static_cast<char>(key));
    }
    if ((key & 0xffU) >= 0xc0U) {
        std::string out;
        for (int shift = 0; shift < 32; shift += 8) {
            const auto byte = static_cast<char>((key >> shift) & 0xffU);
            if (!byte) break;
            out.push_back(byte);
        }
        return out;
    }
    return {};
}

const char* describe_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ESC: return "Esc";
        case LV_KEY_ENTER: return "Enter";
        case LV_KEY_UP: return "Up";
        case LV_KEY_DOWN: return "Down";
        case LV_KEY_LEFT: return "Left";
        case LV_KEY_RIGHT: return "Right";
        case kKeyPageUp: return "PageUp";
        case kKeyPageDown: return "PageDown";
        case kKeyPrintScreen: return "PrintScreen";
        case kKeyHelp: return "Help";
        default: break;
    }
    static char buffer[24];
    const auto text = key_text(key);
    if (!text.empty()) {
        std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
    } else {
        std::snprintf(buffer, sizeof(buffer), "Key_%u", key);
    }
    return buffer;
}

} // namespace platform
