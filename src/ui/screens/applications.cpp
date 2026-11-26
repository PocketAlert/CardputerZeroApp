#include "form_screen.h"
#include "resource_screen.h"

#include "parse.h"
#include "tr.h"

#include <algorithm>

namespace ui {
namespace {

std::string level_tag(const std::optional<int>& level) {
    if (!level) return "";
    auto name = level_label(*level);
    if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

std::vector<std::pair<std::string, std::string>> color_presets() {
    return {
        {"#3B82F6", tr("applications.colors.blue")},   {"#3F5CAD", tr("applications.colors.indigo")},
        {"#E5484D", tr("applications.colors.red")},    {"#F0512C", tr("applications.colors.coral")},
        {"#F59E0B", tr("applications.colors.amber")},  {"#16A34A", tr("applications.colors.green")},
        {"#14B8A6", tr("applications.colors.teal")},   {"#8B5CF6", tr("applications.colors.purple")},
        {"#EC4899", tr("applications.colors.pink")},   {"#6B7280", tr("applications.colors.gray")},
    };
}

// ---------------------------------------------------------------- applications

class ApplicationFormScreen final : public FormScreen {
public:
    ApplicationFormScreen(Ctx& ctx, std::optional<pa::Application> existing)
        : FormScreen(ctx, existing ? tr("applications.form.editTitle") : tr("applications.form.newTitle")), existing_(std::move(existing)) {}

    void build() override {
        chrome_.set_keys({tr("app.save"), "", "", "", tr("app.cancel")});
        form_.create(chrome_.body());
        attach(form_);
        auto colors = color_presets();
        std::string color = existing_ ? existing_->color : "#3B82F6";
        const bool known = std::any_of(colors.begin(), colors.end(), [&](const auto& c) {
            return pa::to_lower(c.first) == pa::to_lower(color);
        });
        if (!known && !color.empty()) colors.insert(colors.begin(), {color, tr("applications.colors.custom", {{"color", color}})});
        for (const auto& c : colors) {
            if (pa::to_lower(c.first) == pa::to_lower(color)) color = c.first;
        }

        std::vector<Field> fields;
        fields.push_back({"name", tr("applications.fields.name"), Field::Kind::Text, existing_ ? existing_->name : "", {}, false,
                          tr("applications.fields.required")});
        fields.push_back({"description", tr("applications.fields.description"), Field::Kind::Text,
                          existing_ ? existing_->description : "", {}, false, tr("applications.fields.optional")});
        fields.push_back({"color", tr("applications.fields.color"), Field::Kind::Choice, color, colors});
        fields.push_back({"level", tr("applications.fields.defaultLevel"), Field::Kind::Choice,
                          existing_ && existing_->default_level ? std::to_string(*existing_->default_level) : "",
                          level_options(tr("applications.fields.levelNotSet"))});
        Field save;
        save.id = "save";
        save.label = existing_ ? tr("applications.form.saveChanges") : tr("applications.form.create");
        save.kind = Field::Kind::Button;
        save.action = [this] { submit(); };
        fields.push_back(save);
        if (existing_) {
            Field info;
            info.id = "tid";
            info.kind = Field::Kind::Note;
            info.value = tr("applications.form.tidNote", {{"tid", existing_->tid}});
            fields.push_back(info);
        }
        form_.set_fields(std::move(fields));
    }

private:
    void submit() override {
        pa::ApplicationInput input;
        const auto name = pa::trim(form_.value("name"));
        if (name.empty()) {
            ctx_.toast(tr("applications.toast.nameRequired"), ToastKind::Error);
            form_.focus("name");
            return;
        }
        const auto description = pa::trim(form_.value("description"));
        const auto color = form_.value("color");
        const auto level_text = form_.value("level");
        const std::optional<int> level = pa::parse_int(level_text);

        if (!existing_ || existing_->name != name) input.name = name;
        if (!existing_ || existing_->description != description) input.description = description;
        if (!existing_ || pa::to_lower(existing_->color) != pa::to_lower(color)) input.color = color;
        if (level && (!existing_ || existing_->default_level != level)) input.default_level = level;

        if (existing_ && !input.name && !input.description && !input.color && !input.default_level) {
            close_self();
            return;
        }
        const auto tid = existing_ ? existing_->tid : "";
        run_once<pa::Result<pa::Empty>>(
            [input, tid](pa::Api& api) {
                if (tid.empty()) {
                    const auto created = api.create_application(input);
                    pa::Result<pa::Empty> out;
                    out.ok = created.ok;
                    out.error = created.error;
                    return out;
                }
                return api.update_application(tid, input);
            },
            [this](pa::Result<pa::Empty>& result) {
                if (!result.ok) {
                    ctx_.show_error(result.error, tr("applications.toast.notSaved"));
                    return;
                }
                ctx_.toast(existing_ ? tr("applications.toast.updated") : tr("applications.toast.created"), ToastKind::Success);
                ctx_.load_lookups();
                close_self();
            });
    }

    std::optional<pa::Application> existing_;
};

class ApplicationsScreen final : public ResourceScreen<pa::Application> {
public:
    explicit ApplicationsScreen(Ctx& ctx)
        : ResourceScreen(ctx, tr("applications.title"), 32, [](pa::Api& api) { return api.applications(); },
                         tr("applications.empty.title"), tr("applications.empty.hint"), icon::kApps) {}

protected:
    void setup() override { chrome_.set_keys({tr("applications.keys.new"), tr("applications.keys.inbox"), tr("applications.keys.send"),
                          tr("applications.keys.edit"), tr("app.delete")}); }

    void loaded() override { ctx_.load_lookups(); }

    void build_row(lv_obj_t* row, const pa::Application& app) override {
        const auto& t = theme();
        auto* column = standard_row(row, nullptr, {}, app.name, app.description.empty() ? app.tid : app.description,
                                    level_tag(app.default_level), level_color(app.default_level.value_or(0)));
        auto* dot = make_box(row);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, parse_hex_color(app.color, t.p.muted), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_move_to_index(dot, 0);
        LV_UNUSED(column);
    }

    bool handle(const Key& key) override {
        auto* app = selected();
        const int slot = key.slot();
        if (slot == 0 || key.is_char('n')) {
            ctx_.push(make_application_form(ctx_, std::nullopt));
            return true;
        }
        if (!app) return false;
        if (slot == 1) {
            ctx_.push(make_inbox(ctx_, InboxFilter{app->tid, app->name}));
            return true;
        }
        if (slot == 2) {
            ctx_.push(make_compose(ctx_, ComposePreset{app->tid, ""}));
            return true;
        }
        if (slot == 3 || key.enter()) {
            ctx_.push(make_application_form(ctx_, *app));
            return true;
        }
        if (slot == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            const auto tid = app->tid;
            confirm_remove(tr("applications.confirm.deleteTitle", {{"name", app->name}}), tr("applications.confirm.deleteText"),
                           [tid](pa::Api& api) { return api.delete_application(tid); },
                           [this] { ctx_.load_lookups(); });
            return true;
        }
        return false;
    }

    std::string help() const override {
        return tr("applications.help");
    }
};

} // namespace

std::unique_ptr<Screen> make_applications(Ctx& ctx) {
    return std::make_unique<ApplicationsScreen>(ctx);
}

std::unique_ptr<Screen> make_application_form(Ctx& ctx, std::optional<pa::Application> existing) {
    return std::make_unique<ApplicationFormScreen>(ctx, std::move(existing));
}

} // namespace ui
