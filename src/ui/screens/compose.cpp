#include "screens.h"

#include "modals.h"
#include "notify.h"
#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

class ComposeScreen final : public Screen {
public:
    ComposeScreen(Ctx& ctx, ComposePreset preset) : Screen(ctx, tr("compose.title")), preset_(std::move(preset)) {}

    void build() override {
        chrome_.set_keys({tr("compose.keys.send"), tr("compose.keys.clear"), "", "", tr("app.cancel")});
        form_.create(chrome_.body());
        attach(form_);
        form_.on_change = [this](const std::string& id) {
            if (id == "when") update_when();
        };
        reset_fields();
        if (ctx_.apps().empty() && ctx_.devices().empty()) {
            std::weak_ptr<bool> alive = alive_;
            ctx_.load_lookups([this, alive] {
                if (!alive.lock()) return;
                form_.set_options("app", ctx_.app_options(tr("compose.options.noApp")));
                form_.set_options("device", ctx_.device_options());
            });
        }
    }

    bool on_key(const Key& key) override {
        if (form_.editing()) return form_.handle_key(key);
        switch (key.slot()) {
            case 0: send(); return true;
            case 1: confirm_clear(); return true;
            case 4: leave(); return true;
            default: break;
        }
        if (key.esc()) {
            leave();
            return true;
        }
        return form_.handle_key(key);
    }

    std::string help() const override { return compose_help(); }

private:
    void reset_fields() {
        std::vector<Field> fields;
        fields.push_back({"title", tr("compose.fields.title"), Field::Kind::Text, "", {}, false, tr("compose.placeholders.required")});
        fields.push_back({"message", tr("compose.fields.message"), Field::Kind::Text, "", {}, false, tr("compose.placeholders.required")});
        fields.push_back({"app", tr("compose.fields.app"), Field::Kind::Choice, preset_.app_tid, ctx_.app_options(tr("compose.options.noApp"))});
        fields.push_back({"device", tr("compose.fields.device"), Field::Kind::Choice, preset_.device_tid, ctx_.device_options()});
        fields.push_back({"level", tr("compose.fields.level"), Field::Kind::Choice, "", level_options(tr("compose.options.levelAppDefault"))});
        fields.push_back({"when", tr("compose.fields.when"), Field::Kind::Choice, "now",
                          {{"now", tr("compose.options.when.now")},
                           {"in", tr("compose.options.when.in")},
                           {"at", tr("compose.options.when.at")}}});
        Field delay{"delay", tr("compose.fields.delay"), Field::Kind::Text, "", {}, false, "30m, 2h, 1d12h"};
        delay.visible = false;
        fields.push_back(delay);
        Field at{"at", tr("compose.fields.at"), Field::Kind::Text, "", {}, false, "YYYY-MM-DD HH:MM"};
        at.visible = false;
        fields.push_back(at);
        fields.push_back({"image", tr("compose.fields.image"), Field::Kind::Text, "", {}, false, tr("compose.placeholders.image")});
        fields.push_back({"action1", tr("compose.fields.action1"), Field::Kind::Text, "", {}, false, "view:Open:https://…"});
        fields.push_back({"action2", tr("compose.fields.action2"), Field::Kind::Text, "", {}, false, "http:Ack:POST https://…"});
        fields.push_back({"action3", tr("compose.fields.action3"), Field::Kind::Text, "", {}, false, "copy:Code:123456"});
        Field note;
        note.id = "note";
        note.kind = Field::Kind::Note;
        note.value = tr("compose.note");
        fields.push_back(note);
        form_.set_fields(std::move(fields));
    }

    void update_when() {
        const auto when = form_.value("when");
        form_.set_visible("delay", when == "in");
        form_.set_visible("at", when == "at");
        if (when != "now") form_.focus(when == "in" ? "delay" : "at");
    }

    bool dirty() const {
        return !pa::trim(form_.value("title")).empty() || !pa::trim(form_.value("message")).empty();
    }

    void leave() {
        if (!dirty()) {
            close_self();
            return;
        }
        ctx_.open_modal(confirm_modal(ctx_, tr("compose.confirm.discard.title"), tr("compose.confirm.discard.text"),
                                      tr("compose.confirm.discard.button"), true,
                                      [this] { close_self(); }));
    }

    void confirm_clear() {
        if (!dirty()) return;
        ctx_.open_modal(confirm_modal(ctx_, tr("compose.confirm.clear.title"), tr("compose.confirm.clear.text"),
                                      tr("compose.confirm.clear.button"), false,
                                      [this] { reset_fields(); }));
    }

    void send() {
        if (submitting()) return;
        pa::MessageCreate message;
        message.title = pa::trim(form_.value("title"));
        message.message = pa::trim(form_.value("message"));
        if (message.title.empty() || message.message.empty()) {
            ctx_.toast(tr("compose.toast.required"), ToastKind::Error);
            form_.focus(message.title.empty() ? "title" : "message");
            return;
        }
        message.application_id = form_.value("app");
        message.device_id = form_.value("device");
        message.level = pa::parse_int(form_.value("level"));

        const auto when = form_.value("when");
        pa::ParseError error;
        if (when == "in") {
            const auto delay = pa::normalize_delay(form_.value("delay"), error);
            if (!delay) {
                ctx_.toast(tr("compose.toast.delayError", {{"error", error.message}}), ToastKind::Error);
                form_.focus("delay");
                return;
            }
            message.delay = *delay;
        } else if (when == "at") {
            message.send_at = pa::trim(form_.value("at"));
            if (message.send_at.empty()) {
                ctx_.toast(tr("compose.toast.atRequired"), ToastKind::Error);
                form_.focus("at");
                return;
            }
        }

        message.image = pa::trim(form_.value("image"));
        if (!message.image.empty() && !pa::is_http_url(message.image)) {
            ctx_.toast(tr("compose.toast.imageUrl"), ToastKind::Error);
            form_.focus("image");
            return;
        }

        for (int i = 1; i <= 3; ++i) {
            const auto id = "action" + std::to_string(i);
            const auto spec = pa::trim(form_.value(id));
            if (spec.empty()) continue;
            auto action = pa::parse_action(spec, false, error);
            if (!action) {
                ctx_.toast(tr("compose.toast.actionError", {{"number", std::to_string(i)}, {"error", error.message}}), ToastKind::Error);
                form_.focus(id);
                return;
            }
            message.actions.push_back(*action);
        }

        run_once<pa::Result<pa::MessageCreated>>(
            [message](pa::Api& api) { return api.send(message); },
            [this](pa::Result<pa::MessageCreated>& result) {
                if (!result.ok) {
                    ctx_.show_error(result.error, tr("compose.errors.notSent"));
                    return;
                }
                if (result.value.scheduled) {
                    ctx_.toast(tr("compose.toast.scheduledFor", {{"time", result.value.deliver_at}}), ToastKind::Success);
                } else {
                    ctx_.toast(tr("compose.toast.sent"), ToastKind::Success);
                }
                if (ctx_.config().sound) pa::play_sound(pa::Sound::Sent);
                ctx_.poll_now();
                close_self();
            });
    }

    ComposePreset preset_;
    FormView form_;
};

class MenuScreen final : public Screen {
public:
    explicit MenuScreen(Ctx& ctx) : Screen(ctx, tr("menu.title")) {}

    void build() override {
        chrome_.set_keys({tr("menu.keys.inbox"), tr("menu.keys.new"), "", "", tr("menu.keys.setup")});
        entries_ = {
            {tr("menu.items.inbox"), icon::kTray, [this] { close_self(); }},
            {tr("menu.items.compose"), icon::kSend, [this] { ctx_.push(make_compose(ctx_)); }},
            {tr("menu.items.scheduled"), icon::kCalendar, [this] { ctx_.push(make_scheduled(ctx_)); }},
            {tr("menu.items.applications"), icon::kApps, [this] { ctx_.push(make_applications(ctx_)); }},
            {tr("menu.items.devices"), icon::kDevice, [this] { ctx_.push(make_devices(ctx_)); }},
            {tr("menu.items.webhooks"), icon::kWebhook, [this] { ctx_.push(make_webhooks(ctx_)); }},
            {tr("menu.items.forms"), icon::kForm, [this] { ctx_.push(make_forms(ctx_)); }},
            {tr("menu.items.apiKeys"), icon::kKey, [this] { ctx_.push(make_api_keys(ctx_)); }},
            {tr("menu.items.account"), icon::kUser, [this] { ctx_.push(make_account(ctx_)); }},
            {tr("menu.items.settings"), icon::kGear, [this] { ctx_.push(make_settings(ctx_)); }},
            {tr("menu.items.help"), icon::kInfo, [this] { ctx_.push(make_shortcuts(ctx_)); }},
            {tr(ctx_.config().demo ? "menu.items.leaveDemo" : "menu.items.signOut"), icon::kSignOut, [this] { confirm_sign_out(); }, true},
        };
        list_.create(chrome_.body(), 24);
        render();
    }

    void on_resume() override { render(); }

    bool on_key(const Key& key) override {
        if (list_.handle_key(key)) return true;
        if (key.enter()) {
            auto action = entries_[list_.selected()].action;
            action();
            return true;
        }
        switch (key.slot()) {
            case 0: close_self(); return true;
            case 1: ctx_.push(make_compose(ctx_)); return true;
            case 4: ctx_.push(make_settings(ctx_)); return true;
            default: return false;
        }
    }

private:
    struct Entry {
        std::string label;
        const char* glyph;
        std::function<void()> action;
        // Destructive entries sit below a divider and draw in the danger color.
        bool apart = false;
    };

    void confirm_sign_out() {
        const bool demo = ctx_.config().demo;
        const std::string prefix = demo ? "menu.confirm.leaveDemo." : "menu.confirm.signOut.";
        ctx_.open_modal(confirm_modal(ctx_, tr(prefix + "title"), tr(prefix + "text"), tr(prefix + "button"), !demo,
                                      [this] { ctx_.sign_out(); }));
    }

    std::string hint_for(size_t index) const {
        switch (index) {
            case 0: return ctx_.unread() > 0 ? tr("menu.hints.unread", {{"count", std::to_string(ctx_.unread())}}) : "";
            case 3: return ctx_.apps().empty() ? "" : std::to_string(ctx_.apps().size());
            case 4: return ctx_.devices().empty() ? "" : std::to_string(ctx_.devices().size());
            case 8: return ctx_.config().demo ? std::string(tr("menu.hints.demo")) : ctx_.config().account_email;
            default: return "";
        }
    }

    void render() {
        list_.set_items(entries_.size(), [this](lv_obj_t* row, size_t i) {
            const auto& t = theme();
            set_row_flex(row, 8);
            const bool apart = entries_[i].apart;
            // ListView owns row borders, so the divider is a gap under the previous row's rule.
            if (apart) lv_obj_set_style_margin_top(row, 8, 0);
            make_icon(row, entries_[i].glyph, apart ? t.p.danger : t.p.accent, t.f.icon_small);
            auto* label = make_label(row, entries_[i].label, t.f.body, apart ? t.p.danger : t.p.text);
            lv_obj_set_flex_grow(label, 1);
            const auto hint = hint_for(i);
            if (!hint.empty()) {
                auto* h = make_label(row, hint, t.f.small, t.p.muted);
                lv_obj_set_style_max_width(h, 140, 0);
                single_line(h);
            }
        }, list_.selected());
    }

    std::vector<Entry> entries_;
    ListView list_;
};

} // namespace

std::string compose_help() {
    return std::string(tr("compose.title")) + "\n" + tr("compose.help.edit") + "\n" + tr("compose.help.change") +
           "\n" + tr("compose.help.keys") + "\n\n" + tr("compose.help.actions") +
           "\n"
           "view:Open:https://example.com\n"
           "http:Restart:POST https://host/hook\n"
           "copy:Code:123456\n\n" +
           tr("compose.help.formats", {{"delay", "30m, 2h, 1d12h"}, {"at", "2026-12-01 09:00"}});
}

std::unique_ptr<Screen> make_compose(Ctx& ctx, ComposePreset preset) {
    return std::make_unique<ComposeScreen>(ctx, std::move(preset));
}

std::unique_ptr<Screen> make_menu(Ctx& ctx) {
    return std::make_unique<MenuScreen>(ctx);
}

} // namespace ui
