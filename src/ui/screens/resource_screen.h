#pragma once

#include "modals.h"
#include "screens.h"
#include "tr.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

// A loadable list of one API resource. The fetch function is stateless on
// purpose: it runs on the worker thread and must not reach into the screen.
template <typename T>
class ResourceScreen : public Screen {
public:
    using Fetch = std::function<pa::Result<std::vector<T>>(pa::Api&)>;

    ResourceScreen(Ctx& ctx, std::string title, int row_height, Fetch fetch, std::string empty_title,
                   std::string empty_hint, const char* glyph)
        : Screen(ctx, std::move(title)),
          row_height_(row_height),
          fetch_(std::move(fetch)),
          empty_title_(std::move(empty_title)),
          empty_hint_(std::move(empty_hint)),
          glyph_(glyph) {}

    void build() override {
        setup();
        list_.create(chrome_.body(), row_height_);
        list_.set_empty(tr("app.loading"), "", glyph_);
        render();
        reload();
    }

    void on_resume() override { reload(); }

    bool on_key(const Key& key) override {
        if (list_.handle_key(key)) return true;
        if (key.is_char('g')) {
            reload();
            return true;
        }
        return handle(key);
    }

protected:
    virtual void setup() = 0;
    virtual void build_row(lv_obj_t* row, const T& item) = 0;
    virtual bool handle(const Key& key) = 0;
    virtual void loaded() {}

    void reload() {
        run<pa::Result<std::vector<T>>>(fetch_, [this](pa::Result<std::vector<T>>& result) {
            if (!result.ok) {
                if (items_.empty()) list_.set_empty(tr("resource.empty.cantLoad"), result.error.text(), icon::kWifiOff);
                ctx_.show_error(result.error);
                render();
                return;
            }
            items_ = std::move(result.value);
            list_.set_empty(empty_title_, empty_hint_, glyph_);
            render();
            loaded();
        });
    }

    void render() {
        list_.set_items(items_.size(), [this](lv_obj_t* row, size_t i) { build_row(row, items_[i]); },
                        list_.selected());
        if (!items_.empty()) {
            chrome_.set_subtitle(std::to_string(items_.size()), theme().p.muted);
        } else {
            chrome_.clear_subtitle();
        }
    }

    T* selected() {
        if (items_.empty() || list_.selected() >= items_.size()) return nullptr;
        return &items_[list_.selected()];
    }

    void confirm_remove(const std::string& title, const std::string& text,
                        std::function<pa::Result<pa::Empty>(pa::Api&)> call, std::function<void()> after = {}) {
        ctx_.open_modal(confirm_modal(ctx_, title, text, tr("app.delete"), true, [this, call, after] {
            run_once<pa::Result<pa::Empty>>(call, [this, after](pa::Result<pa::Empty>& result) {
                if (!result.ok) {
                    ctx_.show_error(result.error);
                    return;
                }
                ctx_.toast(tr("resource.toast.deleted"), ToastKind::Success);
                if (after) after();
                reload();
            });
        }));
    }

    // Two-line row: optional leading icon or colour dot, title, subtitle and a right-hand tag.
    lv_obj_t* standard_row(lv_obj_t* row, const char* glyph, lv_color_t glyph_color, const std::string& title,
                           const std::string& subtitle, const std::string& tag = "", lv_color_t tag_color = {}) {
        const auto& t = theme();
        set_row_flex(row, 7);
        if (glyph) make_icon(row, glyph, glyph_color, t.f.icon);
        auto* column = make_box(row);
        lv_obj_set_height(column, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(column, 1);
        set_column_flex(column, 0);
        auto* title_label = make_label(column, title, t.f.body_bold, t.p.text);
        lv_obj_set_width(title_label, lv_pct(100));
        single_line(title_label);
        if (!subtitle.empty()) {
            auto* sub = make_label(column, subtitle, t.f.small, t.p.muted);
            lv_obj_set_width(sub, lv_pct(100));
            single_line(sub);
        }
        if (!tag.empty()) {
            make_label(row, tag, t.f.small_bold, tag_color);
        }
        return column;
    }

    ListView list_;
    std::vector<T> items_;

private:
    int row_height_;
    Fetch fetch_;
    std::string empty_title_;
    std::string empty_hint_;
    const char* glyph_;
};

} // namespace ui
