#include "widgets.h"

#include "tr.h"

namespace ui {
namespace {
constexpr int32_t kKeyLabelW = 44;
} // namespace

void Chrome::create(lv_obj_t* root, const std::string& title) {
    const auto& t = theme();
    lv_obj_set_style_bg_color(root, t.p.bg, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    auto* header = make_box(root);
    lv_obj_set_size(header, kScreenW, kHeaderH);
    lv_obj_set_style_bg_color(header, t.p.header, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header, 6, 0);
    set_row_flex(header, 5);

    auto* mark = make_icon(header, icon::kBell, t.name == "lcd" ? t.p.bg : t.p.coral, t.f.icon_small);
    LV_UNUSED(mark);
    title_ = make_label(header, title, t.f.small_bold, t.name == "lcd" ? t.p.bg : t.p.text);
    single_line(title_);
    lv_obj_set_style_max_width(title_, 150, 0);
    subtitle_ = make_label(header, "", t.f.small, t.p.muted);
    single_line(subtitle_);
    lv_obj_set_style_max_width(subtitle_, 90, 0);
    lv_obj_add_flag(subtitle_, LV_OBJ_FLAG_HIDDEN);

    auto* spacer = make_box(header);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);

    busy_ = make_icon(header, icon::kRefresh, t.name == "lcd" ? t.p.bg : t.p.accent, t.f.icon_small);
    lv_obj_add_flag(busy_, LV_OBJ_FLAG_HIDDEN);

    status_ = make_box(header);
    lv_obj_set_size(status_, LV_SIZE_CONTENT, kHeaderH);
    set_row_flex(status_, 3);

    body_ = make_box(root);
    lv_obj_set_pos(body_, 0, kBodyY);
    lv_obj_set_size(body_, kScreenW, kBodyH);

    auto* keybar = make_box(root);
    lv_obj_set_pos(keybar, 0, kScreenH - kKeybarH);
    lv_obj_set_size(keybar, kScreenW, kKeybarH);
    lv_obj_set_style_bg_color(keybar, t.p.keybar, 0);
    lv_obj_set_style_bg_opa(keybar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(keybar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(keybar, 1, 0);
    lv_obj_set_style_border_color(keybar, t.p.border, 0);

    // Slot centres line up with the physical 4..8 keys under the display.
    for (size_t i = 0; i < slots_.size(); ++i) {
        auto* slot = make_box(keybar);
        lv_obj_set_size(slot, 56, kKeybarH - 1);
        lv_obj_set_pos(slot, 21 + static_cast<int>(i) * 56, 0);
        lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(slot, 2, 0);

        auto* chip = make_box(slot);
        lv_obj_set_size(chip, 11, 12);
        lv_obj_set_style_radius(chip, 2, 0);
        lv_obj_set_style_bg_color(chip, t.p.key_chip, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        auto* digit = make_label(chip, std::to_string(4 + i), t.f.tiny, t.name == "lcd" ? t.p.bg : t.p.text);
        lv_obj_center(digit);

        auto* label = make_label(slot, "", t.f.small, t.p.text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(label, kKeyLabelW, 0);

        auto* tick = make_box(slot);
        lv_obj_add_flag(tick, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(tick, 1, 2);
        lv_obj_align(tick, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(tick, t.p.faint, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);

        lv_obj_add_flag(slot, LV_OBJ_FLAG_HIDDEN);
        slots_[i] = slot;
    }
}

void Chrome::set_title(const std::string& title) {
    lv_label_set_text(title_, title.c_str());
}

void Chrome::set_subtitle(const std::string& text, lv_color_t color) {
    lv_label_set_text(subtitle_, text.c_str());
    lv_obj_set_style_text_color(subtitle_, color, 0);
    lv_obj_remove_flag(subtitle_, LV_OBJ_FLAG_HIDDEN);
}

void Chrome::clear_subtitle() {
    lv_obj_add_flag(subtitle_, LV_OBJ_FLAG_HIDDEN);
}

void Chrome::set_keys(const std::array<std::string, 5>& labels) {
    for (size_t i = 0; i < slots_.size(); ++i) {
        auto* label = lv_obj_get_child(slots_[i], 1);
        if (labels[i].empty()) {
            lv_obj_add_flag(slots_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            // Translations run longer than the English; step down to the tiny font before clipping.
            const auto& t = theme();
            lv_point_t size{};
            lv_text_get_size(&size, labels[i].c_str(), t.f.small, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_obj_set_style_text_font(label, size.x > kKeyLabelW ? t.f.tiny : t.f.small, 0);
            lv_label_set_text(label, labels[i].c_str());
            lv_obj_remove_flag(slots_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void Chrome::set_status(const StatusInfo& s) {
    if (has_status_ && s == shown_) return;
    has_status_ = true;
    shown_ = s;
    const auto& t = theme();
    const bool lcd = t.name == "lcd";
    const auto fg = lcd ? t.p.bg : t.p.muted;
    lv_obj_clean(status_);
    if (s.demo) {
        make_label(status_, tr("ui.status.demo"), t.f.tiny, lcd ? t.p.bg : t.p.warn);
    }
    if (s.offline) {
        make_icon(status_, icon::kWifiOff, lcd ? t.p.bg : t.p.danger, t.f.icon_small);
    }
    if (s.unread > 0) {
        make_icon(status_, icon::kEnvelope, lcd ? t.p.bg : t.p.accent, t.f.icon_small);
        make_label(status_, std::to_string(s.unread) + (s.unread_overflow ? "+" : ""), t.f.small_bold,
                   lcd ? t.p.bg : t.p.text);
    }
    if (s.wifi_present && !s.offline) {
        make_icon(status_, s.wifi_connected ? icon::kWifi : icon::kWifiOff, fg, t.f.icon_small);
    }
    if (s.battery >= 0) {
        const char* glyph = s.charging          ? icon::kBatteryCharging
                            : s.battery >= 90   ? icon::kBatteryFull
                            : s.battery >= 60   ? icon::kBatteryHigh
                            : s.battery >= 30   ? icon::kBatteryMedium
                                                : icon::kBatteryLow;
        make_icon(status_, glyph, s.battery < 15 && !lcd ? t.p.danger : fg, t.f.icon_small);
    }
    if (!s.clock.empty()) {
        make_label(status_, s.clock, t.f.small, fg);
    }
}

void Chrome::set_busy(bool busy) {
    if (busy) {
        lv_obj_remove_flag(busy_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(busy_, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace ui
