#include "screens.h"

#include "tr.h"

#include <array>

namespace ui {
namespace {

lv_obj_t* make_scroll(lv_obj_t* body) {
    const auto& t = theme();
    auto* scroll = make_box(body);
    lv_obj_set_size(scroll, kScreenW, kBodyH);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(scroll, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(scroll, t.p.muted, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_hor(scroll, 8, 0);
    lv_obj_set_style_pad_ver(scroll, 5, 0);
    set_column_flex(scroll, 3);
    return scroll;
}

lv_obj_t* make_wrapped(lv_obj_t* parent, const std::string& text, const lv_font_t* font, lv_color_t color) {
    auto* label = make_label(parent, text, font, color);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    return label;
}

bool scroll_key(lv_obj_t* scroll, const Key& key) {
    if (key.down() || key.up()) {
        lv_obj_scroll_by_bounded(scroll, 0, key.down() ? -26 : 26, LV_ANIM_OFF);
        return true;
    }
    if (key.page_down() || key.page_up() || key.is_char(' ')) {
        lv_obj_scroll_by_bounded(scroll, 0, key.page_up() ? 100 : -100, LV_ANIM_OFF);
        return true;
    }
    if (key.is_char('t') || key.is_char('T') || key.is(LV_KEY_HOME)) {
        lv_obj_scroll_to_y(scroll, 0, LV_ANIM_OFF);
        return true;
    }
    return false;
}

struct Page {
    const char* glyph;
    const char* key;
};

constexpr std::array<Page, 4> kPages{{
    {icon::kBellRinging, "guide.pages.welcome"},
    {icon::kList, "guide.pages.keys"},
    {icon::kTray, "guide.pages.inbox"},
    {icon::kInfo, "guide.pages.help"},
}};

class WelcomeScreen final : public Screen {
public:
    explicit WelcomeScreen(Ctx& ctx)
        : Screen(ctx, tr("guide.pages.welcome.title")), on_launch_(ctx.config().tour_on_launch) {}

    void build() override { render(); }

    bool on_key(const Key& key) override {
        if (scroll_key(scroll_, key)) return true;
        if (key.enter() || key.right() || key.slot() == 4) {
            if (page_ + 1 < kPages.size()) {
                ++page_;
                render();
            } else {
                finish();
            }
            return true;
        }
        if ((key.left() || key.slot() == 0) && page_ > 0) {
            --page_;
            render();
            return true;
        }
        if (key.esc() || key.slot() == 3) {
            finish();
            return true;
        }
        if (last_page() && key.slot() == 1) {
            on_launch_ = !on_launch_;
            render();
            return true;
        }
        return true;
    }

private:
    void render() {
        const auto& t = theme();
        const auto& page = kPages[page_];
        const std::string prefix = page.key;
        const bool last = last_page();
        chrome_.set_title(tr(prefix + ".title"));
        chrome_.set_subtitle(std::to_string(page_ + 1) + "/" + std::to_string(kPages.size()), t.p.muted);
        chrome_.set_keys({page_ > 0 ? std::string(tr("guide.keys.back")) : std::string(),
                          last ? std::string(tr("guide.keys.toggle")) : std::string(), "",
                          last ? std::string() : std::string(tr("guide.keys.skip")),
                          tr(last ? "guide.keys.done" : "guide.keys.next")});

        auto* body = chrome_.body();
        lv_obj_clean(body);
        scroll_ = make_scroll(body);
        lv_obj_set_style_pad_row(scroll_, 6, 0);
        make_icon(scroll_, page.glyph, t.p.accent, t.f.icon_big);
        make_wrapped(scroll_, tr(prefix + ".text"), t.f.body, t.p.text);
        if (last) {
            auto* row = make_box(scroll_);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_margin_top(row, 4, 0);
            set_row_flex(row, 6);
            auto* box = make_box(row);
            lv_obj_set_size(box, 14, 14);
            lv_obj_set_style_radius(box, 3, 0);
            lv_obj_set_style_border_width(box, 1, 0);
            lv_obj_set_style_border_color(box, on_launch_ ? t.p.accent : t.p.muted, 0);
            lv_obj_set_style_bg_color(box, t.p.accent, 0);
            lv_obj_set_style_bg_opa(box, on_launch_ ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            if (on_launch_) {
                auto* check = make_icon(box, icon::kCheck, t.p.text, t.f.icon_small);
                lv_obj_center(check);
            }
            auto* label = make_label(row, std::string("5  ") + tr("guide.launch"), t.f.small, t.p.muted);
            lv_obj_set_flex_grow(label, 1);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        }
    }

    bool last_page() const { return page_ + 1 == kPages.size(); }

    void finish() {
        auto& config = ctx_.config();
        if (!config.onboarded || config.tour_on_launch != on_launch_) {
            config.onboarded = true;
            config.tour_on_launch = on_launch_;
            ctx_.save_config();
        }
        close_self();
    }

    size_t page_ = 0;
    bool on_launch_;
    lv_obj_t* scroll_ = nullptr;
};

class ShortcutsScreen final : public Screen {
public:
    explicit ShortcutsScreen(Ctx& ctx) : Screen(ctx, tr("guide.shortcuts.title")) {}

    void build() override {
        const auto& t = theme();
        chrome_.set_keys({tr("guide.keys.tour"), "", "", "", tr("app.back")});
        scroll_ = make_scroll(chrome_.body());
        const std::array<std::string, 7> sections{
            tr("ui.help.global"),   tr("inbox.help"),        tr("message.help"),  compose_help(),
            scheduled_help(),       tr("applications.help"), tr("webhooks.help"),
        };
        for (size_t i = 0; i < sections.size(); ++i) {
            const auto& text = sections[i];
            const auto split = text.find('\n');
            auto* heading = make_wrapped(scroll_, text.substr(0, split), t.f.small_bold, t.p.accent);
            if (i > 0) lv_obj_set_style_margin_top(heading, 6, 0);
            if (split != std::string::npos) make_wrapped(scroll_, text.substr(split + 1), t.f.small, t.p.text);
        }
    }

    bool on_key(const Key& key) override {
        if (scroll_key(scroll_, key)) return true;
        if (key.slot() == 0) {
            ctx_.push(make_welcome(ctx_));
            return true;
        }
        if (key.slot() == 4) {
            close_self();
            return true;
        }
        return false;
    }

private:
    lv_obj_t* scroll_ = nullptr;
};

} // namespace

std::unique_ptr<Screen> make_welcome(Ctx& ctx) {
    return std::make_unique<WelcomeScreen>(ctx);
}

std::unique_ptr<Screen> make_shortcuts(Ctx& ctx) {
    return std::make_unique<ShortcutsScreen>(ctx);
}

} // namespace ui
