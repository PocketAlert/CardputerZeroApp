#include "modals.h"

#include "theme.h"
#include "tr.h"
#include "widgets.h"

namespace ui {

Modal::Modal(Ctx& ctx) : ctx_(ctx) {}

Modal::~Modal() {
    if (overlay_) {
        lv_obj_delete(overlay_);
    }
}

lv_obj_t* Modal::make_panel(const std::string& title, const char* glyph, lv_color_t glyph_color, int height) {
    const auto& t = theme();
    overlay_ = make_box(lv_layer_top());
    lv_obj_set_size(overlay_, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_50, 0);

    panel_ = make_box(overlay_);
    lv_obj_set_size(panel_, kScreenW - 12, height);
    lv_obj_center(panel_);
    lv_obj_set_style_bg_color(panel_, t.p.surface, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel_, 6, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);
    lv_obj_set_style_border_color(panel_, t.p.border, 0);
    lv_obj_set_style_pad_all(panel_, 6, 0);
    set_column_flex(panel_, 4);

    auto* header = make_box(panel_);
    lv_obj_set_size(header, lv_pct(100), 16);
    set_row_flex(header, 5);
    if (glyph) {
        make_icon(header, glyph, glyph_color, t.f.icon_small);
    }
    auto* title_label = make_label(header, title, t.f.small_bold, t.p.text);
    single_line(title_label);
    lv_obj_set_flex_grow(title_label, 1);

    auto* content = make_box(panel_);
    lv_obj_set_width(content, lv_pct(100));
    // A content-sized panel cannot hand out leftover space, so its body sizes itself too.
    if (height == LV_SIZE_CONTENT) {
        lv_obj_set_height(content, LV_SIZE_CONTENT);
    } else {
        lv_obj_set_flex_grow(content, 1);
    }

    footer_ = make_label(panel_, "", t.f.small, t.p.faint);
    lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
    return content;
}

void Modal::set_footer(const std::string& text) {
    lv_label_set_text(footer_, text.c_str());
    lv_obj_remove_flag(footer_, LV_OBJ_FLAG_HIDDEN);
}

void Modal::close() {
    ctx_.close_modal(this);
}

namespace {

class ConfirmModal final : public Modal {
public:
    ConfirmModal(Ctx& ctx, const std::string& title, const std::string& text, const std::string& confirm_label,
                 bool danger, std::function<void()> on_yes)
        : Modal(ctx), on_yes_(std::move(on_yes)) {
        const auto& t = theme();
        auto* content = make_panel(title, danger ? icon::kWarning : icon::kInfo, danger ? t.p.danger : t.p.accent, 110);
        auto* label = make_label(content, text, t.f.body, t.p.text);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);

        auto* buttons = make_box(panel_);
        lv_obj_set_size(buttons, lv_pct(100), 22);
        lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(buttons, 6, 0);
        make_button(buttons, tr("ui.modal.cancelButton"), t.p.raised, t.p.muted);
        make_button(buttons, tr("ui.modal.confirmButton", {{"label", confirm_label}}), danger ? t.p.danger : t.p.accent,
                    danger ? lv_color_hex(0xffffff) : t.p.on_accent);
    }

    bool on_key(const Key& key) override {
        if (key.enter() || key.is_char('y')) {
            auto action = on_yes_;
            close();
            if (action) action();
            return true;
        }
        if (key.esc() || key.is_char('n')) {
            close();
        }
        return true;
    }

private:
    static void make_button(lv_obj_t* parent, const std::string& text, lv_color_t bg, lv_color_t fg) {
        auto* button = make_box(parent);
        lv_obj_set_size(button, LV_SIZE_CONTENT, 20);
        lv_obj_set_style_pad_hor(button, 8, 0);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_bg_color(button, bg, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        auto* label = make_label(button, text, theme().f.small_bold, fg);
        lv_obj_center(label);
    }

    std::function<void()> on_yes_;
};

class InfoModal final : public Modal {
public:
    InfoModal(Ctx& ctx, const std::string& title, const std::string& text, const std::string& qr_payload,
              const char* glyph)
        : Modal(ctx) {
        const auto& t = theme();
        auto* content = make_panel(title, glyph ? glyph : icon::kInfo, t.p.accent);
        set_row_flex(content, 8);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        if (!qr_payload.empty()) {
            make_qr(content, qr_payload, 104);
        }
        scroll_ = make_box(content);
        lv_obj_set_height(scroll_, lv_pct(100));
        lv_obj_set_flex_grow(scroll_, 1);
        lv_obj_add_flag(scroll_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(scroll_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(scroll_, LV_SCROLLBAR_MODE_ACTIVE);
        auto* label = make_label(scroll_, text, t.f.body, t.p.text);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        set_footer(tr("ui.modal.infoFooter"));
    }

    bool on_key(const Key& key) override {
        if (key.down() || key.page_down()) {
            lv_obj_scroll_by_bounded(scroll_, 0, key.page_down() ? -80 : -24, LV_ANIM_OFF);
        } else if (key.up() || key.page_up()) {
            lv_obj_scroll_by_bounded(scroll_, 0, key.page_up() ? 80 : 24, LV_ANIM_OFF);
        } else if (key.enter() || key.esc()) {
            close();
        }
        return true;
    }

private:
    lv_obj_t* scroll_ = nullptr;
};

class PickerModal final : public Modal {
public:
    PickerModal(Ctx& ctx, const std::string& title, std::vector<PickItem> items, size_t selected,
                std::function<void(size_t)> on_pick)
        : Modal(ctx), items_(std::move(items)), on_pick_(std::move(on_pick)) {
        const auto& t = theme();
        auto* content = make_panel(title, icon::kList, t.p.accent);
        list_.create(content, 22);
        list_.set_items(items_.size(), [this](lv_obj_t* row, size_t i) { build_row(row, i); }, selected);
        set_footer(tr("ui.modal.pickerFooter"));
    }

    bool on_key(const Key& key) override {
        if (list_.handle_key(key)) return true;
        if (key.enter() && !list_.empty()) {
            auto action = on_pick_;
            const auto index = list_.selected();
            close();
            if (action) action(index);
            return true;
        }
        if (key.esc()) close();
        return true;
    }

private:
    void build_row(lv_obj_t* row, size_t i) {
        const auto& t = theme();
        const auto& item = items_[i];
        set_row_flex(row, 6);
        if (item.has_color) {
            auto* dot = make_box(row);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, item.color, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        } else if (item.glyph) {
            make_icon(row, item.glyph, t.p.muted, t.f.icon_small);
        }
        auto* label = make_label(row, item.label, t.f.body, t.p.text);
        lv_obj_set_flex_grow(label, 1);
        single_line(label);
        if (!item.hint.empty()) {
            make_label(row, item.hint, t.f.small, t.p.muted);
        }
    }

    std::vector<PickItem> items_;
    std::function<void(size_t)> on_pick_;
    ListView list_;
};

class PromptModal final : public Modal {
public:
    PromptModal(Ctx& ctx, const std::string& title, const std::string& hint, const std::string& initial, bool secret,
                std::function<void(const std::string&)> on_submit)
        : Modal(ctx), on_submit_(std::move(on_submit)) {
        const auto& t = theme();
        auto* content = make_panel(title, icon::kPencil, t.p.accent, LV_SIZE_CONTENT);
        set_column_flex(content, 6);
        if (!hint.empty()) {
            auto* label = make_label(content, hint, t.f.small, t.p.muted);
            lv_obj_set_width(label, lv_pct(100));
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        }
        input_ = make_textarea(content, initial, secret, true);
        lv_obj_set_size(input_, lv_pct(100), 22);
        set_footer(tr("ui.modal.promptFooter"));
    }

    bool on_key(const Key& key) override {
        if (key.enter()) {
            const std::string value = lv_textarea_get_text(input_);
            auto action = on_submit_;
            close();
            if (action) action(value);
            return true;
        }
        if (key.esc()) {
            close();
            return true;
        }
        edit_text(input_, key);
        return true;
    }

private:
    lv_obj_t* input_ = nullptr;
    std::function<void(const std::string&)> on_submit_;
};

} // namespace

std::unique_ptr<Modal> confirm_modal(Ctx& ctx, const std::string& title, const std::string& text,
                                     const std::string& confirm_label, bool danger, std::function<void()> on_yes) {
    return std::make_unique<ConfirmModal>(ctx, title, text, confirm_label, danger, std::move(on_yes));
}

std::unique_ptr<Modal> info_modal(Ctx& ctx, const std::string& title, const std::string& text,
                                  const std::string& qr_payload, const char* glyph) {
    return std::make_unique<InfoModal>(ctx, title, text, qr_payload, glyph);
}

std::unique_ptr<Modal> picker_modal(Ctx& ctx, const std::string& title, std::vector<PickItem> items, size_t selected,
                                    std::function<void(size_t)> on_pick) {
    return std::make_unique<PickerModal>(ctx, title, std::move(items), selected, std::move(on_pick));
}

std::unique_ptr<Modal> prompt_modal(Ctx& ctx, const std::string& title, const std::string& hint,
                                    const std::string& initial, bool secret,
                                    std::function<void(const std::string&)> on_submit) {
    return std::make_unique<PromptModal>(ctx, title, hint, initial, secret, std::move(on_submit));
}

} // namespace ui
