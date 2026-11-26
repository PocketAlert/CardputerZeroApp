#include "widgets.h"

#include "tr.h"

#include <algorithm>
#include <ctime>
#include <cstdio>

namespace ui {

lv_obj_t* make_box(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* make_label(lv_obj_t* parent, const std::string& text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text.c_str());
    return label;
}

lv_obj_t* make_icon(lv_obj_t* parent, const char* glyph, lv_color_t color, const lv_font_t* font) {
    return make_label(parent, glyph, font ? font : theme().f.icon, color);
}

lv_obj_t* make_textarea(lv_obj_t* parent, const std::string& text, bool secret, bool one_line) {
    const auto& t = theme();
    auto* ta = lv_textarea_create(parent);
    lv_obj_remove_style_all(ta);
    lv_obj_set_style_text_font(ta, t.f.body, 0);
    lv_obj_set_style_text_color(ta, t.p.text, 0);
    lv_obj_set_style_bg_color(ta, t.p.raised, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ta, 3, 0);
    lv_obj_set_style_pad_hor(ta, 4, 0);
    lv_obj_set_style_pad_ver(ta, 2, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, t.p.accent, 0);
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 1, static_cast<lv_style_selector_t>(LV_PART_CURSOR) | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, t.p.accent, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta, 500, LV_PART_CURSOR);
    lv_obj_set_style_text_color(ta, t.p.faint, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(ta, t.f.body, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_one_line(ta, one_line);
    lv_textarea_set_password_mode(ta, secret);
    lv_textarea_set_password_show_time(ta, 600);
    lv_textarea_set_text(ta, text.c_str());
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    return ta;
}

lv_obj_t* make_qr(lv_obj_t* parent, const std::string& payload, int size) {
    auto* qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xffffff));
    lv_qrcode_update(qr, payload.data(), static_cast<uint32_t>(payload.size()));
    lv_obj_set_style_border_color(qr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(qr, 3, 0);
    return qr;
}

void set_row_flex(lv_obj_t* obj, int gap) {
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, gap, 0);
}

void single_line(lv_obj_t* label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

void set_column_flex(lv_obj_t* obj, int gap) {
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(obj, gap, 0);
}

bool edit_text(lv_obj_t* textarea, const Key& key) {
    if (key.printable()) {
        lv_textarea_add_text(textarea, key.text.c_str());
        return true;
    }
    switch (key.code) {
        case LV_KEY_BACKSPACE: lv_textarea_delete_char(textarea); return true;
        case LV_KEY_DEL: lv_textarea_delete_char_forward(textarea); return true;
        case LV_KEY_LEFT: lv_textarea_cursor_left(textarea); return true;
        case LV_KEY_RIGHT: lv_textarea_cursor_right(textarea); return true;
        case LV_KEY_HOME: lv_textarea_set_cursor_pos(textarea, 0); return true;
        case LV_KEY_END: lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST); return true;
        default: return false;
    }
}

std::string short_time(const std::string& created_at) {
    int day = 0, month = 0, year = 0, hour = 0, minute = 0;
    if (std::sscanf(created_at.c_str(), "%d.%d.%d %d:%d", &day, &month, &year, &hour, &minute) != 5) {
        return created_at.substr(0, 10);
    }
    std::tm then{};
    then.tm_mday = day;
    then.tm_mon = month - 1;
    then.tm_year = year - 1900;
    then.tm_isdst = -1;
    const std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_r(&now, &today);
    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    const double days = std::difftime(std::mktime(&today), std::mktime(&then)) / 86400.0;

    char buffer[16];
    if (days < 0.5) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    } else if (days < 1.5) {
        return tr("ui.time.yesterday");
    } else if (days < 6.5) {
        constexpr const char* kWeekdays[] = {"ui.time.weekday.sun", "ui.time.weekday.mon", "ui.time.weekday.tue",
                                             "ui.time.weekday.wed", "ui.time.weekday.thu", "ui.time.weekday.fri",
                                             "ui.time.weekday.sat"};
        std::mktime(&then);
        return tr(kWeekdays[std::clamp(then.tm_wday, 0, 6)]);
    } else {
        constexpr const char* kMonths[] = {"ui.time.month.jan", "ui.time.month.feb", "ui.time.month.mar",
                                           "ui.time.month.apr", "ui.time.month.may", "ui.time.month.jun",
                                           "ui.time.month.jul", "ui.time.month.aug", "ui.time.month.sep",
                                           "ui.time.month.oct", "ui.time.month.nov", "ui.time.month.dec"};
        return tr("ui.time.dayMonth",
                  {{"day", std::to_string(day)}, {"month", tr(kMonths[std::clamp(month - 1, 0, 11)])}});
    }
    return buffer;
}

std::string one_line(const std::string& text, size_t max_bytes) {
    std::string out;
    out.reserve(std::min(text.size(), max_bytes + 4));
    bool space = false;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            space = !out.empty();
            continue;
        }
        if (space) {
            out.push_back(' ');
            space = false;
        }
        out.push_back(c);
        if (out.size() >= max_bytes) {
            while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
                out.pop_back();
            }
            if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80)) {
                out.pop_back();
            }
            out += "…";
            break;
        }
    }
    return out;
}

std::string human_size(long long bytes) {
    char buffer[32];
    if (bytes >= 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return tr("ui.size.megabytes", {{"size", buffer}});
    }
    if (bytes >= 1024) {
        return tr("ui.size.kilobytes", {{"size", std::to_string(bytes / 1024)}});
    }
    return tr("ui.size.bytes", {{"size", std::to_string(bytes)}});
}

} // namespace ui
