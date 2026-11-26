#include "widgets.h"

#include <algorithm>

#include "tr.h"

namespace ui {

void ListView::create(lv_obj_t* parent, int row_height) {
    row_height_ = row_height;
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(container_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(container_, theme().p.muted, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(container_, 2, LV_PART_SCROLLBAR);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);
}

void ListView::set_empty(const std::string& title, const std::string& hint, const char* glyph) {
    empty_title_ = title;
    empty_hint_ = hint;
    empty_icon_ = glyph;
}

void ListView::set_items(size_t count, Builder builder, size_t keep_selected) {
    builder_ = std::move(builder);
    lv_obj_clean(container_);
    rows_.clear();
    empty_ = nullptr;

    if (count == 0) {
        const auto& t = theme();
        empty_ = make_box(container_);
        lv_obj_set_size(empty_, lv_pct(100), kBodyH);
        lv_obj_set_flex_flow(empty_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(empty_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(empty_, 4, 0);
        if (empty_icon_) {
            make_icon(empty_, empty_icon_, t.p.faint, t.f.icon_big);
        }
        make_label(empty_, empty_title_.empty() ? std::string(tr("ui.list.empty")) : empty_title_, t.f.body_bold,
                   t.p.muted);
        if (!empty_hint_.empty()) {
            auto* hint = make_label(empty_, empty_hint_, t.f.small, t.p.faint);
            lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        }
        selected_ = 0;
        return;
    }

    rows_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto* row = make_box(container_);
        lv_obj_set_size(row, lv_pct(100), row_height_);
        lv_obj_set_style_pad_hor(row, 6, 0);
        builder_(row, i);
        rows_.push_back(row);
        style_row(i, false);
    }
    selected_ = std::min(keep_selected, count - 1);
    style_row(selected_, true);
    lv_obj_update_layout(container_);
    lv_obj_scroll_to_view(rows_[selected_], LV_ANIM_OFF);
}

void ListView::append_items(size_t count) {
    if (!builder_) return;
    if (empty_ || count < rows_.size()) {
        set_items(count, builder_, selected_);
        return;
    }
    for (size_t i = rows_.size(); i < count; ++i) {
        auto* row = make_box(container_);
        lv_obj_set_size(row, lv_pct(100), row_height_);
        lv_obj_set_style_pad_hor(row, 6, 0);
        builder_(row, i);
        rows_.push_back(row);
        style_row(i, false);
    }
}

void ListView::prepend_items(size_t added) {
    if (added == 0 || !builder_) return;
    if (empty_) {
        set_items(rows_.size() + added, builder_, 0);
        return;
    }
    std::vector<lv_obj_t*> fresh;
    fresh.reserve(added);
    for (size_t i = 0; i < added; ++i) {
        auto* row = make_box(container_);
        lv_obj_set_size(row, lv_pct(100), row_height_);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_move_to_index(row, static_cast<int32_t>(i));
        fresh.push_back(row);
    }
    rows_.insert(rows_.begin(), fresh.begin(), fresh.end());
    for (size_t i = 0; i < added; ++i) {
        builder_(rows_[i], i);
        style_row(i, false);
    }
    selected_ += added;
    lv_obj_update_layout(container_);
    lv_obj_scroll_to_view(rows_[selected_], LV_ANIM_OFF);
}

void ListView::rebuild_row(size_t index) {
    if (index >= rows_.size() || !builder_) {
        return;
    }
    lv_obj_clean(rows_[index]);
    builder_(rows_[index], index);
    style_row(index, index == selected_);
}

void ListView::style_row(size_t index, bool selected) {
    auto* row = rows_[index];
    const auto& t = theme();
    lv_obj_set_style_bg_color(row, t.p.selection, 0);
    lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, t.p.border, 0);
    lv_obj_set_style_border_opa(row, selected ? LV_OPA_TRANSP : LV_OPA_60, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
}

void ListView::select(size_t index) {
    if (rows_.empty()) {
        return;
    }
    index = std::min(index, rows_.size() - 1);
    if (index != selected_) {
        style_row(selected_, false);
        selected_ = index;
        style_row(selected_, true);
    }
    lv_obj_scroll_to_view(rows_[selected_], LV_ANIM_OFF);
    if (on_selection) {
        on_selection(selected_);
    }
    if (on_end_reached && selected_ + 3 >= rows_.size()) {
        on_end_reached();
    }
}

bool ListView::handle_key(const Key& key) {
    if (rows_.empty()) {
        return key.up() || key.down();
    }
    const size_t page = std::max<size_t>(1, static_cast<size_t>(kBodyH / std::max(1, row_height_)));
    if (key.down()) {
        if (selected_ + 1 < rows_.size()) {
            select(selected_ + 1);
        } else if (on_end_reached) {
            on_end_reached();
        }
        return true;
    }
    if (key.up()) {
        select(selected_ == 0 ? 0 : selected_ - 1);
        return true;
    }
    if (key.page_down()) {
        select(selected_ + page);
        return true;
    }
    if (key.page_up()) {
        select(selected_ > page ? selected_ - page : 0);
        return true;
    }
    if (key.is(LV_KEY_HOME) || key.is_char('t') || key.is_char('T')) {
        select(0);
        return true;
    }
    if (key.is(LV_KEY_END)) {
        select(rows_.size() - 1);
        return true;
    }
    return false;
}

} // namespace ui
