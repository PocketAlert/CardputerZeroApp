#include "screens.h"

#include "modals.h"
#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

const char* action_icon(const std::string& type) {
    if (type == "view") return icon::kExternal;
    if (type == "copy") return icon::kCopy;
    return icon::kLightning;
}

class MessageScreen final : public Screen {
public:
    MessageScreen(Ctx& ctx, pa::Message message) : Screen(ctx, tr("message.title")), message_(std::move(message)) {}

    void build() override {
        render();
        const auto tid = message_.tid;
        // Opening the message through the API is also what marks it read server-side.
        run<pa::Result<pa::Message>>([tid](pa::Api& api) { return api.message(tid); },
                                     [this](pa::Result<pa::Message>& result) {
                                         if (!result.ok) {
                                             if (result.error.status == 404) {
                                                 ctx_.note_message_change({message_.tid, true, false});
                                                 ctx_.toast(tr("message.toast.gone"), ToastKind::Error);
                                                 close_self();
                                             }
                                             return;
                                         }
                                         auto fresh = result.value;
                                         if (fresh.device.empty()) fresh.device = message_.device;
                                         if (fresh.application.empty()) fresh.application = message_.application;
                                         message_ = fresh;
                                         message_.is_read = true;
                                         render();
                                     });
    }

    bool on_key(const Key& key) override {
        if (key.down() || key.up()) {
            lv_obj_scroll_by_bounded(scroll_, 0, key.down() ? -26 : 26, LV_ANIM_OFF);
            return true;
        }
        if (key.is_char('t') || key.is_char('T') || key.is(LV_KEY_HOME)) {
            lv_obj_scroll_to_y(scroll_, 0, LV_ANIM_OFF);
            return true;
        }
        if (key.page_down() || key.page_up() || key.is_char(' ')) {
            lv_obj_scroll_by_bounded(scroll_, 0, key.page_up() ? 100 : -100, LV_ANIM_OFF);
            return true;
        }
        const int slot = key.slot();
        if (slot >= 0 && slot < static_cast<int>(buttons_.size())) {
            run_message_action(ctx_, buttons_[static_cast<size_t>(slot)], alive_);
            return true;
        }
        if (slot == 3 || key.is_char('r')) {
            toggle_read();
            return true;
        }
        if (slot == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            confirm_delete();
            return true;
        }
        if (key.is_char('1') || key.is_char('2') || key.is_char('3')) {
            const size_t index = static_cast<size_t>(key.text[0] - '1');
            if (index < buttons_.size()) run_message_action(ctx_, buttons_[index], alive_);
            return true;
        }
        return false;
    }

    std::string help() const override {
        return tr("message.help");
    }

private:
    void render() {
        const auto& t = theme();
        chrome_.set_title(message_.application.empty() ? std::string(tr("message.title")) : message_.application);
        if (message_.priority != 0) {
            chrome_.set_subtitle(level_label(message_.priority), level_color(message_.priority));
        } else {
            chrome_.clear_subtitle();
        }

        buttons_ = message_.actions;
        if (message_.attachment && buttons_.size() < 3 && !message_.attachment->link.empty()) {
            buttons_.push_back({"view", tr("message.attachment"), message_.attachment->link});
        }
        if (buttons_.size() > 3) buttons_.resize(3);
        std::array<std::string, 5> keys{"", "", "", tr(message_.is_read ? "message.keys.unread" : "message.keys.read"),
                                        tr("message.keys.delete")};
        for (size_t i = 0; i < buttons_.size(); ++i) keys[i] = buttons_[i].label;
        chrome_.set_keys(keys);

        auto* body = chrome_.body();
        lv_obj_clean(body);
        scroll_ = make_box(body);
        lv_obj_set_size(scroll_, kScreenW, kBodyH);
        lv_obj_add_flag(scroll_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(scroll_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(scroll_, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_width(scroll_, 2, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(scroll_, t.p.muted, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_hor(scroll_, 8, 0);
        lv_obj_set_style_pad_ver(scroll_, 5, 0);
        set_column_flex(scroll_, 4);

        auto* title = make_label(scroll_, message_.title, t.f.title, t.p.text);
        lv_obj_set_width(title, lv_pct(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);

        std::string meta = message_.created_at;
        if (!message_.device.empty()) meta += "  ·  " + message_.device;
        if (message_.was_scheduled) meta += std::string("  ·  ") + tr("message.meta.scheduled");
        auto* meta_label = make_label(scroll_, meta, t.f.small, t.p.muted);
        lv_obj_set_width(meta_label, lv_pct(100));
        lv_label_set_long_mode(meta_label, LV_LABEL_LONG_MODE_WRAP);

        auto* text = make_label(scroll_, message_.message, t.f.body, t.p.text);
        lv_obj_set_width(text, lv_pct(100));
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_line_space(text, 2, 0);
        lv_obj_set_style_pad_top(text, 3, 0);

        if (message_.attachment) {
            const auto& a = *message_.attachment;
            auto* row = make_box(scroll_);
            lv_obj_set_size(row, lv_pct(100), 18);
            set_row_flex(row, 5);
            make_icon(row, icon::kPaperclip, t.p.muted, t.f.icon_small);
            auto* name = make_label(row, a.file_name.empty() ? std::string(tr("message.attachment")) : a.file_name, t.f.small, t.p.text);
            single_line(name);
            lv_obj_set_flex_grow(name, 1);
            if (a.file_size > 0) make_label(row, human_size(a.file_size), t.f.small, t.p.muted);
        }

        if (!buttons_.empty()) {
            auto* heading = make_label(scroll_, tr("message.actionsHeading"), t.f.tiny, t.p.faint);
            lv_obj_set_style_pad_top(heading, 4, 0);
            for (size_t i = 0; i < buttons_.size(); ++i) {
                const auto& action = buttons_[i];
                auto* row = make_box(scroll_);
                lv_obj_set_size(row, lv_pct(100), 20);
                lv_obj_set_style_bg_color(row, t.p.raised, 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(row, 4, 0);
                lv_obj_set_style_pad_hor(row, 5, 0);
                set_row_flex(row, 6);

                auto* chip = make_box(row);
                lv_obj_set_size(chip, 11, 12);
                lv_obj_set_style_radius(chip, 2, 0);
                lv_obj_set_style_bg_color(chip, t.p.key_chip, 0);
                lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
                lv_obj_center(make_label(chip, std::to_string(4 + i), t.f.tiny, t.name == "lcd" ? t.p.bg : t.p.text));

                make_icon(row, action_icon(action.type), t.p.accent, t.f.icon_small);
                make_label(row, action.label, t.f.small_bold, t.p.text);
                std::string target = action.value;
                if (action.type == "http") {
                    if (const auto cfg = pa::parse_http_action(action.value)) target = cfg->method + " " + cfg->url;
                }
                auto* hint = make_label(row, one_line(target, 80), t.f.small, t.p.faint);
                lv_obj_set_flex_grow(hint, 1);
                single_line(hint);
            }
        }
    }

    void toggle_read() {
        const bool read = !message_.is_read;
        message_.is_read = read;
        render();
        const auto tid = message_.tid;
        run<pa::Result<pa::Empty>>([tid, read](pa::Api& api) { return api.set_read(tid, read); },
                                   [this, read](pa::Result<pa::Empty>& result) {
                                       if (!result.ok) {
                                           message_.is_read = !read;
                                           render();
                                           ctx_.show_error(result.error);
                                           return;
                                       }
                                       ctx_.note_message_change({message_.tid, false, read});
                                       ctx_.toast(tr(read ? "message.toast.markedRead" : "message.toast.markedUnread"), ToastKind::Success);
                                   });
    }

    void confirm_delete() {
        const auto tid = message_.tid;
        ctx_.open_modal(confirm_modal(ctx_, tr("message.confirm.delete.title"), one_line(message_.title, 90), tr("app.delete"), true, [this, tid] {
            run_once<pa::Result<pa::Empty>>([tid](pa::Api& api) { return api.delete_message(tid); },
                                       [this, tid](pa::Result<pa::Empty>& result) {
                                           if (!result.ok) {
                                               ctx_.show_error(result.error);
                                               return;
                                           }
                                           ctx_.note_message_change({tid, true, false});
                                           ctx_.toast(tr("message.toast.deleted"), ToastKind::Success);
                                           close_self();
                                       });
        }));
    }

    pa::Message message_;
    std::vector<pa::Action> buttons_;
    lv_obj_t* scroll_ = nullptr;
};

} // namespace

std::unique_ptr<Screen> make_message(Ctx& ctx, pa::Message message) {
    return std::make_unique<MessageScreen>(ctx, std::move(message));
}

} // namespace ui
