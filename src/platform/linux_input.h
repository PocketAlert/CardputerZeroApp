#pragma once

#include "lvgl.h"

#include <cstdint>
#include <string>

namespace platform {

constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U;
constexpr uint32_t kKeyHelp = kSpecialKeyBase | 19U;
constexpr uint32_t kKeyPageUp = kSpecialKeyBase | 20U;
constexpr uint32_t kKeyPageDown = kSpecialKeyBase | 21U;

// Every key from every source (evdev on the device, SDL and the clickable
// keypad in the simulator) arrives here as: press, optional long press,
// repeats while held (repeat = 1, 2, … at a rising rate), release.
using KeyHandler = void (*)(uint32_t key, bool long_pressed, bool released, uint32_t repeat, void* user_data);

void init_key_input(lv_display_t* display);
// Sleeps until a key arrives, wake_fd becomes readable or timeout_ms passes,
// feeding any keys to LVGL before it returns. On the device this is the only
// place the main loop blocks, so an idle app costs no CPU.
void wait_for_events(uint32_t timeout_ms, int wake_fd);
void attach_key_router(lv_indev_t* indev);
void set_key_handler(KeyHandler handler, void* user_data);

bool is_special_key(uint32_t key);
// Printable text for a key: ASCII, or UTF-8 bytes packed little-endian (SDL text input).
std::string key_text(uint32_t key);
const char* describe_key(uint32_t key);

} // namespace platform
