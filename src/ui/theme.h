#pragma once

#include "lvgl.h"

#include <string>

namespace app {
class AssetManager;
}

namespace ui {

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr int kHeaderH = 20;
constexpr int kKeybarH = 22;
constexpr int kBodyY = kHeaderH;
constexpr int kBodyH = kScreenH - kHeaderH - kKeybarH;

struct Palette {
    lv_color_t bg;
    lv_color_t surface;
    lv_color_t raised;
    lv_color_t border;
    lv_color_t text;
    lv_color_t muted;
    lv_color_t faint;
    lv_color_t accent;
    lv_color_t on_accent;
    lv_color_t selection;
    lv_color_t danger;
    lv_color_t coral;
    lv_color_t warn;
    lv_color_t ok;
    lv_color_t header;
    lv_color_t keybar;
    lv_color_t key_chip;
};

struct Fonts {
    const lv_font_t* body = LV_FONT_DEFAULT;
    const lv_font_t* body_bold = LV_FONT_DEFAULT;
    const lv_font_t* small = LV_FONT_DEFAULT;
    const lv_font_t* small_bold = LV_FONT_DEFAULT;
    const lv_font_t* tiny = LV_FONT_DEFAULT;
    const lv_font_t* title = LV_FONT_DEFAULT;
    const lv_font_t* big = LV_FONT_DEFAULT;
    const lv_font_t* mono = LV_FONT_DEFAULT;
    const lv_font_t* icon = LV_FONT_DEFAULT;
    const lv_font_t* icon_small = LV_FONT_DEFAULT;
    const lv_font_t* icon_big = LV_FONT_DEFAULT;
};

struct Theme {
    Palette p;
    Fonts f;
    std::string name = "dark";
};

Theme& theme();
void load_fonts(app::AssetManager& assets);
void set_palette(const std::string& name);
void apply_to_display(lv_display_t* display);

lv_color_t level_color(int level);
const char* level_icon(int level);
lv_color_t parse_hex_color(const std::string& hex, lv_color_t fallback);

namespace icon {
constexpr const char* kBell = "";
constexpr const char* kBellRinging = "";
constexpr const char* kEnvelope = "";
constexpr const char* kEnvelopeOpen = "";
constexpr const char* kSend = "";
constexpr const char* kClock = "";
constexpr const char* kCalendar = "";
constexpr const char* kApps = "";
constexpr const char* kDevice = "";
constexpr const char* kWebhook = "";
constexpr const char* kForm = "";
constexpr const char* kKey = "";
constexpr const char* kUser = "";
constexpr const char* kGear = "";
constexpr const char* kTrash = "";
constexpr const char* kCheck = "";
constexpr const char* kCheckCircle = "";
constexpr const char* kEye = "";
constexpr const char* kFunnel = "";
constexpr const char* kSearch = "";
constexpr const char* kList = "";
constexpr const char* kSignOut = "";
constexpr const char* kWarning = "";
constexpr const char* kWarningCircle = "";
constexpr const char* kInfo = "";
constexpr const char* kCopy = "";
constexpr const char* kLink = "";
constexpr const char* kGlobe = "";
constexpr const char* kLightning = "";
constexpr const char* kChart = "";
constexpr const char* kPencil = "";
constexpr const char* kPlus = "";
constexpr const char* kWifi = "";
constexpr const char* kWifiOff = "";
constexpr const char* kQr = "";
constexpr const char* kPaperclip = "";
constexpr const char* kTray = "";
constexpr const char* kRefresh = "";
constexpr const char* kExternal = "";
constexpr const char* kXCircle = "";
constexpr const char* kLock = "";
constexpr const char* kAt = "";
constexpr const char* kCircle = "";
constexpr const char* kCaretRight = "";
constexpr const char* kPower = "";
constexpr const char* kSpeaker = "";
constexpr const char* kSpeakerOff = "";
constexpr const char* kTimer = "";
constexpr const char* kMoon = "";
constexpr const char* kSun = "";
constexpr const char* kChat = "";
constexpr const char* kImage = "";
constexpr const char* kCursor = "";
constexpr const char* kBroadcast = "";
constexpr const char* kBatteryFull = "";
constexpr const char* kBatteryHigh = "";
constexpr const char* kBatteryMedium = "";
constexpr const char* kBatteryLow = "";
constexpr const char* kBatteryCharging = "";
} // namespace icon

} // namespace ui
