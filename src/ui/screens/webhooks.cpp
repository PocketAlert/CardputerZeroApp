#include "form_screen.h"
#include "resource_screen.h"

#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

// ---------------------------------------------------------------- webhooks

class WebhookFormScreen final : public FormScreen {
public:
    WebhookFormScreen(Ctx& ctx, std::optional<pa::Webhook> existing)
        : FormScreen(ctx, existing ? tr("webhooks.form.editTitle") : tr("webhooks.form.newTitle")), existing_(std::move(existing)) {}

    void build() override {
        chrome_.set_keys({tr("app.save"), "", "", "", tr("app.cancel")});
        form_.create(chrome_.body());
        attach(form_);
        std::vector<pa::Action> actions;
        if (existing_) actions = pa::parse_actions_field(pa::json(existing_->actions));

        std::vector<Field> fields;
        fields.push_back({"name", tr("webhooks.fields.name"), Field::Kind::Text, existing_ ? existing_->name : "", {}, false,
                          tr("webhooks.fields.required")});
        fields.push_back({"template", tr("webhooks.fields.template"), Field::Kind::Text, existing_ ? existing_->message : "*",
                          {}, false, tr("webhooks.fields.templatePlaceholder")});
        fields.push_back({"slug", tr("webhooks.fields.slug"), Field::Kind::Text, existing_ ? existing_->url : "", {}, false,
                          tr("webhooks.fields.slugPlaceholder")});
        fields.push_back({"app", tr("webhooks.fields.application"), Field::Kind::Choice, existing_ ? existing_->application_tid : "",
                          ctx_.app_options(tr("webhooks.fields.applicationNone"))});
        fields.push_back({"device", tr("webhooks.fields.device"), Field::Kind::Choice, existing_ ? existing_->device_tid : "",
                          ctx_.device_options()});
        fields.push_back({"level", tr("webhooks.fields.level"), Field::Kind::Choice,
                          existing_ && existing_->default_level ? std::to_string(*existing_->default_level) : "",
                          level_options(tr("webhooks.fields.levelNotSet"))});
        for (int i = 0; i < 3; ++i) {
            const auto value = static_cast<size_t>(i) < actions.size() ? pa::format_action_spec(actions[static_cast<size_t>(i)]) : "";
            fields.push_back({"action" + std::to_string(i + 1),
                              tr("webhooks.fields.action", {{"number", std::to_string(i + 1)}}), Field::Kind::Text,
                              value, {}, false, i == 0 ? "view:Open:%url%" : ""});
        }
        if (existing_) {
            Field active{"active", tr("webhooks.fields.active"), Field::Kind::Toggle};
            active.on = existing_->is_active;
            fields.push_back(active);
        }
        Field save;
        save.id = "save";
        save.label = existing_ ? tr("webhooks.form.saveChanges") : tr("webhooks.form.create");
        save.kind = Field::Kind::Button;
        save.action = [this] { submit(); };
        fields.push_back(save);
        Field note;
        note.id = "note";
        note.kind = Field::Kind::Note;
        note.value = tr("webhooks.form.note");
        fields.push_back(note);
        form_.set_fields(std::move(fields));
    }

private:
    void submit() override {
        pa::WebhookInput input;
        const auto name = pa::trim(form_.value("name"));
        if (name.empty()) {
            ctx_.toast(tr("webhooks.toast.nameRequired"), ToastKind::Error);
            form_.focus("name");
            return;
        }
        auto tmpl = pa::trim(form_.value("template"));
        if (tmpl.empty()) tmpl = "*";
        const auto slug = pa::trim(form_.value("slug"));
        const auto app = form_.value("app");
        const auto device = form_.value("device");
        const auto level_text = form_.value("level");

        std::vector<pa::Action> actions;
        pa::ParseError error;
        for (int i = 1; i <= 3; ++i) {
            const auto id = "action" + std::to_string(i);
            const auto spec = pa::trim(form_.value(id));
            if (spec.empty()) continue;
            auto action = pa::parse_action(spec, true, error);
            if (!action) {
                ctx_.toast(tr("webhooks.toast.actionInvalid", {{"number", std::to_string(i)}, {"error", error.message}}),
                           ToastKind::Error);
                form_.focus(id);
                return;
            }
            actions.push_back(*action);
        }

        if (!existing_ || existing_->name != name) input.name = name;
        if (!existing_ || existing_->message != tmpl) input.message = tmpl;
        if (!slug.empty() && (!existing_ || existing_->url != slug)) input.slug = slug;
        if (!existing_ || existing_->application_tid != app) input.application_id = app;
        if (!existing_ || existing_->device_tid != device) input.device_id = device;
        if (const auto level = pa::parse_int(level_text)) {
            if (!existing_ || existing_->default_level != *level) input.default_level = *level;
        }
        const auto existing_actions = existing_ ? pa::parse_actions_field(pa::json(existing_->actions)) : std::vector<pa::Action>{};
        const bool actions_changed =
            actions.size() != existing_actions.size() ||
            !std::equal(actions.begin(), actions.end(), existing_actions.begin(), [](const auto& a, const auto& b) {
                return a.type == b.type && a.label == b.label && a.value == b.value;
            });
        if (actions_changed && (!actions.empty() || existing_)) input.actions = actions;
        if (existing_ && form_.toggled("active") != existing_->is_active) input.is_active = form_.toggled("active");

        const auto tid = existing_ ? existing_->tid : "";
        run_once<pa::Result<pa::Webhook>>(
            [input, tid](pa::Api& api) {
                if (tid.empty()) return api.create_webhook(input);
                const auto updated = api.update_webhook(tid, input);
                pa::Result<pa::Webhook> out;
                out.ok = updated.ok;
                out.error = updated.error;
                return out;
            },
            [this](pa::Result<pa::Webhook>& result) {
                if (!result.ok) {
                    ctx_.show_error(result.error, tr("webhooks.toast.notSaved"));
                    return;
                }
                if (!existing_) {
                    const auto url = pa::kWebhookBaseUrl + result.value.url;
                    close_self();
                    ctx_.open_modal(info_modal(ctx_, tr("webhooks.created.title"),
                                               tr("webhooks.created.text", {{"url", url}}), url, icon::kWebhook));
                    return;
                }
                ctx_.toast(tr("webhooks.toast.updated"), ToastKind::Success);
                close_self();
            });
    }

    std::optional<pa::Webhook> existing_;
};

class WebhookHistoryScreen final : public ResourceScreen<pa::WebhookRequest> {
public:
    WebhookHistoryScreen(Ctx& ctx, pa::Webhook webhook)
        : ResourceScreen(
              ctx, webhook.name, 32,
              [tid = webhook.tid](pa::Api& api) {
                  auto history = api.webhook_history(tid, 1, 50);
                  pa::Result<std::vector<pa::WebhookRequest>> out;
                  out.ok = history.ok;
                  out.error = history.error;
                  out.value = history.value.requests;
                  return out;
              },
              tr("webhooks.history.empty.title"), tr("webhooks.history.empty.hint"), icon::kBroadcast) {}

protected:
    void setup() override { chrome_.set_keys({tr("webhooks.history.keys.refresh"), "", "", "", ""}); }

    void build_row(lv_obj_t* row, const pa::WebhookRequest& r) override {
        const auto& t = theme();
        const bool ok = r.status == "success";
        std::string summary = ok ? r.rendered_message : (r.error_code.empty() ? std::string(tr("webhooks.history.error")) : r.error_code);
        if (summary.empty()) summary = r.method + " " + human_size(r.size);
        standard_row(row, ok ? icon::kCheckCircle : icon::kXCircle, ok ? t.p.ok : t.p.danger, one_line(summary, 100),
                     tr("webhooks.history.rowMeta", {{"time", short_time(r.created_at)},
                                                     {"method", r.method},
                                                     {"ip", r.source_ip},
                                                     {"duration", std::to_string(r.duration_ms)}}));
    }

    bool handle(const Key& key) override {
        if (key.slot() == 0) {
            reload();
            return true;
        }
        auto* r = selected();
        if (r && key.enter()) {
            const auto status = r->status + (r->error_code.empty() ? "" : " (" + r->error_code + ")");
            std::string text = tr("webhooks.history.detail", {{"status", status},
                                                              {"when", r->created_at},
                                                              {"from", r->source_ip},
                                                              {"type", r->content_type},
                                                              {"body", r->body}});
            if (!r->rendered_message.empty()) {
                text += tr("webhooks.history.detailMessage", {{"message", r->rendered_message}});
            }
            ctx_.open_modal(info_modal(ctx_, tr("webhooks.history.requestTitle", {{"method", r->method}}), text, "",
                                       icon::kBroadcast));
            return true;
        }
        return false;
    }
};

class WebhooksScreen final : public ResourceScreen<pa::Webhook> {
public:
    explicit WebhooksScreen(Ctx& ctx)
        : ResourceScreen(ctx, tr("webhooks.title"), 32, [](pa::Api& api) { return api.webhooks(); },
                         tr("webhooks.empty.title"), tr("webhooks.empty.hint"), icon::kWebhook) {}

protected:
    void setup() override { chrome_.set_keys({tr("webhooks.keys.new"), tr("webhooks.keys.history"), tr("webhooks.keys.url"),
                          tr("webhooks.keys.toggle"), tr("app.delete")}); }

    void build_row(lv_obj_t* row, const pa::Webhook& hook) override {
        const auto& t = theme();
        standard_row(row, icon::kWebhook, hook.is_active ? t.p.accent : t.p.faint, hook.name, "p4a.me/wh/" + hook.url,
                     hook.is_active ? "" : tr("webhooks.row.off"), t.p.muted);
    }

    bool handle(const Key& key) override {
        const int slot = key.slot();
        if (slot == 0 || key.is_char('n')) {
            ctx_.push(make_webhook_form(ctx_, std::nullopt));
            return true;
        }
        auto* hook = selected();
        if (!hook) return false;
        if (key.enter()) {
            ctx_.push(make_webhook_form(ctx_, *hook));
            return true;
        }
        if (slot == 1) {
            ctx_.push(make_webhook_history(ctx_, *hook));
            return true;
        }
        if (slot == 2) {
            const auto url = pa::kWebhookBaseUrl + hook->url;
            const auto curl = "curl -X POST " + url +
                              " -H 'Content-Type: application/json' -d '{\"title\":\"Hi\",\"message\":\"It works\"}'";
            ctx_.open_modal(info_modal(
                ctx_, hook->name,
                tr("webhooks.urlInfo", {{"url", url}, {"template", hook->message}, {"curl", curl}}), url,
                icon::kLink));
            return true;
        }
        if (slot == 3) {
            const auto tid = hook->tid;
            const bool active = !hook->is_active;
            pa::WebhookInput input;
            input.is_active = active;
            run_once<pa::Result<pa::Empty>>([tid, input](pa::Api& api) { return api.update_webhook(tid, input); },
                                       [this, active](pa::Result<pa::Empty>& result) {
                                           if (!result.ok) {
                                               ctx_.show_error(result.error);
                                               return;
                                           }
                                           ctx_.toast(active ? tr("webhooks.toast.enabled") : tr("webhooks.toast.paused"), ToastKind::Success);
                                           reload();
                                       });
            return true;
        }
        if (slot == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            const auto tid = hook->tid;
            confirm_remove(tr("webhooks.confirm.deleteTitle", {{"name", hook->name}}), tr("webhooks.confirm.deleteText"),
                           [tid](pa::Api& api) { return api.delete_webhook(tid); });
            return true;
        }
        return false;
    }

    std::string help() const override {
        return tr("webhooks.help");
    }
};

} // namespace

std::unique_ptr<Screen> make_webhooks(Ctx& ctx) {
    return std::make_unique<WebhooksScreen>(ctx);
}

std::unique_ptr<Screen> make_webhook_form(Ctx& ctx, std::optional<pa::Webhook> existing) {
    return std::make_unique<WebhookFormScreen>(ctx, std::move(existing));
}

std::unique_ptr<Screen> make_webhook_history(Ctx& ctx, pa::Webhook webhook) {
    return std::make_unique<WebhookHistoryScreen>(ctx, std::move(webhook));
}

} // namespace ui
