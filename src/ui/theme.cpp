#include "theme.h"

#include "asset_manager.h"

#include <cstdlib>
#include <filesystem>

namespace ui {
namespace {

Theme g_theme;

lv_color_t hex(uint32_t value) {
    return lv_color_hex(value);
}

// Warm neutrals and accents come from the web design tokens
// (frontend-pocketalert/src/design-system/tokens.css), dark variant.
Palette dark_palette() {
    Palette p{};
    p.bg = hex(0x15140f);
    p.surface = hex(0x1b1a15);
    p.raised = hex(0x26241e);
    p.border = hex(0x322f28);
    p.text = hex(0xf5f1e9);
    p.muted = hex(0x8f897c);
    p.faint = hex(0x5a554b);
    p.accent = hex(0x6f8ad6);
    p.on_accent = hex(0x0f1424);
    p.selection = hex(0x2a2f45);
    p.danger = hex(0xe5484d);
    p.coral = hex(0xf0512c);
    p.warn = hex(0xf59e0b);
    p.ok = hex(0x22c55e);
    p.header = hex(0x0f0e0b);
    p.keybar = hex(0x0f0e0b);
    p.key_chip = hex(0x322f28);
    return p;
}

// The pager LCD look from the landing page hero.
Palette lcd_palette() {
    Palette p{};
    p.bg = hex(0xb6c0b3);
    p.surface = hex(0xadb8aa);
    p.raised = hex(0xa2ad9f);
    p.border = hex(0x8e9a8b);
    p.text = hex(0x1b241c);
    p.muted = hex(0x46524a);
    p.faint = hex(0x6d786c);
    p.accent = hex(0x1b241c);
    p.on_accent = hex(0xb6c0b3);
    p.selection = hex(0x96a293);
    p.danger = hex(0x8f1d1d);
    p.coral = hex(0x8a3410);
    p.warn = hex(0x6b4a07);
    p.ok = hex(0x1f5a32);
    p.header = hex(0x1b241c);
    p.keybar = hex(0xa2ad9f);
    p.key_chip = hex(0x1b241c);
    return p;
}

const lv_font_t* load(app::AssetManager& assets, const char* file, uint32_t size, const lv_font_t* fallback) {
    auto* font = assets.load_font(file, size);
    if (!font) {
        return fallback;
    }
    // Geist has Latin, Cyrillic and Greek. CJK comes from the system's full Noto
    // (fonts-noto-cjk) when installed, so any message renders; otherwise from the
    // bundled subset, which covers only the translated UI strings.
    static const char* system_cjk = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
    std::error_code ec;
    font->fallback = std::filesystem::exists(system_cjk, ec) ? assets.load_font(system_cjk, size)
                                                              : assets.load_font("NotoSansCJK-UI.otf", size);
    return font;
}

} // namespace

Theme& theme() {
    return g_theme;
}

void load_fonts(app::AssetManager& assets) {
    auto& f = g_theme.f;
    f.body = load(assets, "Geist-400.ttf", 13, &lv_font_montserrat_14);
    f.body_bold = load(assets, "Geist-600.ttf", 13, &lv_font_montserrat_14);
    f.small = load(assets, "Geist-400.ttf", 11, &lv_font_montserrat_12);
    f.small_bold = load(assets, "Geist-600.ttf", 11, &lv_font_montserrat_12);
    f.tiny = load(assets, "Geist-500.ttf", 9, &lv_font_montserrat_10);
    f.title = load(assets, "Geist-600.ttf", 15, &lv_font_montserrat_16);
    f.big = load(assets, "Geist-700.ttf", 24, &lv_font_montserrat_18);
    f.mono = load(assets, "Geist-Mono-400.ttf", 11, &lv_font_montserrat_12);
    f.icon = load(assets, "Phosphor-Fill.ttf", 14, &lv_font_montserrat_14);
    f.icon_small = load(assets, "Phosphor-Fill.ttf", 11, &lv_font_montserrat_12);
    f.icon_big = load(assets, "Phosphor-Fill.ttf", 30, &lv_font_montserrat_18);
}

void set_palette(const std::string& name) {
    g_theme.name = name == "lcd" ? "lcd" : "dark";
    g_theme.p = g_theme.name == "lcd" ? lcd_palette() : dark_palette();
}

void apply_to_display(lv_display_t* display) {
    auto* lv_theme = lv_theme_default_init(display, g_theme.p.accent, g_theme.p.coral, g_theme.name != "lcd",
                                           g_theme.f.body);
    lv_display_set_theme(display, lv_theme);
}

lv_color_t level_color(int level) {
    const auto& p = g_theme.p;
    switch (level) {
        case 2: return p.danger;
        case 1: return p.coral;
        case 0: return p.accent;
        case -1: return p.muted;
        default: return p.faint;
    }
}

const char* level_icon(int level) {
    switch (level) {
        case 2: return icon::kWarning;
        case 1: return icon::kWarningCircle;
        case -2: return icon::kSpeakerOff;
        default: return icon::kBell;
    }
}

lv_color_t parse_hex_color(const std::string& text, lv_color_t fallback) {
    std::string digits = text;
    if (!digits.empty() && digits[0] == '#') {
        digits.erase(0, 1);
    }
    if (digits.size() != 6) {
        return fallback;
    }
    char* end = nullptr;
    const auto value = std::strtoul(digits.c_str(), &end, 16);
    if (!end || *end != '\0') {
        return fallback;
    }
    return lv_color_hex(static_cast<uint32_t>(value));
}

} // namespace ui
