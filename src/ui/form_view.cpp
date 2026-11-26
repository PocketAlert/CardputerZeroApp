#include "widgets.h"

#include "tr.h"

#include <algorithm>

namespace ui {

std::string Field::choice_label() const {
    for (const auto& [option_value, option_label] : options) {
        if (option_value == value) {
            return option_label;
        }
    }
    return value.empty() ? (options.empty() ? "" : options.front().second) : value;
}

void FormView::create(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(container_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(container_, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(container_, theme().p.muted, LV_PART_SCROLLBAR);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);
}

void FormView::set_fields(std::vector<Field> fields) {
    fields_ = std::move(fields);
    selected_ = 0;
    ensure_selectable(1);
    render();
}

Field* FormView::find(const std::string& id) {
    for (auto& field : fields_) {
        if (field.id == id) return &field;
    }
    return nullptr;
}

std::string FormView::value(const std::string& id) const {
    for (const auto& field : fields_) {
        if (field.id == id) return field.value;
    }
    return {};
}

bool FormView::toggled(const std::string& id) const {
    for (const auto& field : fields_) {
        if (field.id == id) return field.on;
    }
    return false;
}

void FormView::set_value(const std::string& id, const std::string& new_value) {
    if (auto* field = find(id)) {
        field->value = new_value;
        render();
    }
}

void FormView::set_options(const std::string& id, std::vector<std::pair<std::string, std::string>> options) {
    if (auto* field = find(id)) {
        field->options = std::move(options);
        render();
    }
}

void FormView::set_visible(const std::string& id, bool visible) {
    if (auto* field = find(id); field && field->visible != visible) {
        field->visible = visible;
        ensure_selectable(1);
        render();
    }
}

void FormView::focus(const std::string& id) {
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].id == id && selectable(i)) {
            const auto previous = selected_;
            selected_ = i;
            refresh_selection(previous);
            return;
        }
    }
}

bool FormView::selectable(size_t index) const {
    return index < fields_.size() && fields_[index].visible && fields_[index].kind != Field::Kind::Note;
}

void FormView::ensure_selectable(int direction) {
    if (fields_.empty() || selectable(selected_)) {
        return;
    }
    for (size_t step = 0; step < fields_.size(); ++step) {
        move(direction);
        if (selectable(selected_)) return;
    }
}

void FormView::render() {
    editor_ = nullptr;
    lv_obj_clean(container_);
    rows_.assign(fields_.size(), nullptr);
    value_labels_.assign(fields_.size(), nullptr);

    for (size_t i = 0; i < fields_.size(); ++i) {
        if (!fields_[i].visible) {
            continue;
        }
        auto* row = make_box(container_);
        rows_[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_bg_color(row, theme().p.selection, 0);
        if (fields_[i].divider_above) lv_obj_set_style_margin_top(row, 8, 0);
        fill_row(i);
    }

    lv_obj_update_layout(container_);
    if (selected_ < rows_.size() && rows_[selected_]) {
        lv_obj_scroll_to_view(rows_[selected_], LV_ANIM_OFF);
    }
}

// Builds one row's content in place; moving the selection rebuilds only the
// two rows involved instead of the whole form.
void FormView::fill_row(size_t i) {
    const auto& t = theme();
    const auto& field = fields_[i];
    auto* row = rows_[i];
    if (!row) return;
    lv_obj_clean(row);
    value_labels_[i] = nullptr;
    if (editor_ && i == selected_) editor_ = nullptr;
    const bool selected = i == selected_;
    lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    if (field.kind == Field::Kind::Note) {
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row, 3, 0);
        auto* note = make_label(row, field.value, t.f.small, t.p.muted);
        lv_obj_set_width(note, lv_pct(100));
        lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
        return;
    }

    lv_obj_set_height(row, 24);
    set_row_flex(row, 6);

    if (field.kind == Field::Kind::Button) {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(row, field.label, t.f.body_bold, selected ? t.p.text : t.p.accent);
        return;
    }

    auto* label = make_label(row, field.label, t.f.small, selected ? t.p.text : t.p.muted);
    lv_obj_set_width(label, 78);
    single_line(label);

    std::string shown;
    lv_color_t color = t.p.text;
    switch (field.kind) {
        case Field::Kind::Text:
            shown = field.value.empty() ? field.placeholder : one_line(field.value, 120);
            color = field.value.empty() ? t.p.faint : t.p.text;
            break;
        case Field::Kind::Secret:
            if (field.value.empty()) {
                shown = field.placeholder;
            } else {
                for (size_t n = 0; n < std::min<size_t>(field.value.size(), 16); ++n) shown += "•";
            }
            color = field.value.empty() ? t.p.faint : t.p.text;
            break;
        case Field::Kind::Choice:
            shown = field.choice_label();
            break;
        case Field::Kind::Toggle:
            shown = field.on ? tr("ui.toggle.on") : tr("ui.toggle.off");
            color = field.on ? t.p.ok : t.p.muted;
            break;
        default:
            break;
    }
    auto* value_label = make_label(row, shown, t.f.body, color);
    lv_obj_set_flex_grow(value_label, 1);
    single_line(value_label);
    value_labels_[i] = value_label;

    if (field.kind == Field::Kind::Choice) {
        make_icon(row, icon::kCaretRight, selected ? t.p.accent : t.p.faint, t.f.icon_small);
    } else if (field.kind == Field::Kind::Toggle) {
        make_icon(row, field.on ? icon::kCheckCircle : icon::kCircle, field.on ? t.p.ok : t.p.faint, t.f.icon_small);
    } else if (selected) {
        make_icon(row, icon::kPencil, t.p.faint, t.f.icon_small);
    }
}

void FormView::refresh_selection(size_t previous) {
    if (previous != selected_ && previous < rows_.size()) fill_row(previous);
    if (selected_ < rows_.size() && rows_[selected_]) {
        fill_row(selected_);
        lv_obj_scroll_to_view(rows_[selected_], LV_ANIM_OFF);
    }
}

void FormView::move(int direction) {
    if (fields_.empty()) return;
    const size_t n = fields_.size();
    size_t index = selected_;
    for (size_t step = 0; step < n; ++step) {
        if (direction > 0) {
            if (index + 1 >= n) break;
            ++index;
        } else {
            if (index == 0) break;
            --index;
        }
        if (selectable(index)) {
            selected_ = index;
            break;
        }
    }
}

void FormView::cycle(int direction) {
    auto& field = fields_[selected_];
    if (field.kind == Field::Kind::Toggle) {
        field.on = !field.on;
    } else if (field.kind == Field::Kind::Choice && !field.options.empty()) {
        size_t index = 0;
        for (size_t i = 0; i < field.options.size(); ++i) {
            if (field.options[i].first == field.value) index = i;
        }
        const auto n = field.options.size();
        index = (index + n + static_cast<size_t>(direction > 0 ? 1 : n - 1)) % n;
        field.value = field.options[index].first;
    } else {
        return;
    }
    const auto id = field.id;
    fill_row(selected_);
    if (on_change) on_change(id);
}

void FormView::begin_edit() {
    auto& field = fields_[selected_];
    auto* row = rows_[selected_];
    auto* value_label = value_labels_[selected_];
    if (!row || !value_label) return;
    lv_obj_add_flag(value_label, LV_OBJ_FLAG_HIDDEN);
    // Drop the trailing pencil icon so the editor gets the full width.
    const auto children = lv_obj_get_child_count(row);
    if (children > 2) {
        lv_obj_add_flag(lv_obj_get_child(row, static_cast<int32_t>(children - 1)), LV_OBJ_FLAG_HIDDEN);
    }
    editor_ = make_textarea(row, field.value, field.kind == Field::Kind::Secret, true);
    lv_obj_set_flex_grow(editor_, 1);
    lv_obj_set_height(editor_, 20);
    lv_textarea_set_placeholder_text(editor_, field.placeholder.c_str());
}

void FormView::end_edit(bool commit) {
    if (!editor_) return;
    auto& field = fields_[selected_];
    const bool changed = commit && field.value != lv_textarea_get_text(editor_);
    if (commit) {
        field.value = lv_textarea_get_text(editor_);
    }
    const auto id = field.id;
    editor_ = nullptr;
    fill_row(selected_);
    if (changed && on_change) on_change(id);
}

bool FormView::handle_key(const Key& key) {
    if (fields_.empty()) return false;

    if (editor_) {
        if (key.enter() || key.esc()) {
            end_edit(true);
        } else if (key.tab() || key.is(LV_KEY_DOWN) || key.shift_tab() || key.is(LV_KEY_UP)) {
            end_edit(true);
            const auto previous = selected_;
            move(key.tab() || key.is(LV_KEY_DOWN) ? 1 : -1);
            refresh_selection(previous);
        } else {
            edit_text(editor_, key);
        }
        // Swallow everything while typing so digits and letters never trigger shortcuts.
        return true;
    }

    auto& field = fields_[selected_];
    if (key.down() || key.tab() || key.up() || key.shift_tab()) {
        const auto previous = selected_;
        move(key.down() || key.tab() ? 1 : -1);
        refresh_selection(previous);
        return true;
    }
    if (key.left() || key.right()) {
        if (field.kind == Field::Kind::Choice || field.kind == Field::Kind::Toggle) {
            cycle(key.right() ? 1 : -1);
            return true;
        }
        return false;
    }
    if (key.enter() || key.is_char(' ')) {
        switch (field.kind) {
            case Field::Kind::Text:
            case Field::Kind::Secret:
                begin_edit();
                return true;
            case Field::Kind::Toggle:
                cycle(1);
                return true;
            case Field::Kind::Choice:
                if (on_choose) {
                    const auto id = field.id;
                    on_choose(field, [this, id](const std::string& chosen) {
                        for (size_t i = 0; i < fields_.size(); ++i) {
                            if (fields_[i].id != id) continue;
                            fields_[i].value = chosen;
                            fill_row(i);
                            if (on_change) on_change(id);
                            return;
                        }
                    });
                } else {
                    cycle(1);
                }
                return true;
            case Field::Kind::Button:
                if (field.action) {
                    auto action = field.action;
                    action();
                }
                return true;
            default:
                return false;
        }
    }
    if (key.backspace() && (field.kind == Field::Kind::Text || field.kind == Field::Kind::Secret)) {
        begin_edit();
        if (editor_) lv_textarea_delete_char(editor_);
        return true;
    }
    return false;
}

} // namespace ui
