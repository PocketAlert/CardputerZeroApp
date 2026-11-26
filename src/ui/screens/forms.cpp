#include "resource_screen.h"

#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

// ---------------------------------------------------------------- forms

class FormsScreen final : public ResourceScreen<pa::Form> {
public:
    explicit FormsScreen(Ctx& ctx)
        : ResourceScreen(ctx, tr("forms.title"), 32, [](pa::Api& api) { return api.forms(); },
                         tr("forms.empty.title"), tr("forms.empty.hint"), icon::kForm) {}

protected:
    void setup() override { chrome_.set_keys({tr("forms.keys.link"), "", "", "", tr("app.delete")}); }

    void build_row(lv_obj_t* row, const pa::Form& form) override {
        const auto& t = theme();
        const auto count = std::to_string(form.submissions_count);
        const auto sub = form.last_submission_at.empty()
                             ? tr("forms.row.submissions", {{"count", count}})
                             : tr("forms.row.submissionsLast",
                                  {{"count", count}, {"time", short_time(form.last_submission_at)}});
        standard_row(row, icon::kForm, form.closed ? t.p.faint : t.p.accent, form.name, sub,
                     form.closed ? tr("forms.row.closed") : "", t.p.muted);
    }

    bool handle(const Key& key) override {
        auto* form = selected();
        if (!form) return false;
        if (key.slot() == 0 || key.enter()) {
            const auto url = pa::kFormBaseUrl + form->tid;
            ctx_.open_modal(info_modal(ctx_, form->name,
                                       tr("forms.link.text",
                                          {{"url", url}, {"count", std::to_string(form->submissions_count)}}),
                                       url, icon::kLink));
            return true;
        }
        if (key.slot() == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            const auto tid = form->tid;
            confirm_remove(tr("forms.confirm.deleteTitle", {{"name", form->name}}), tr("forms.confirm.deleteText"),
                           [tid](pa::Api& api) { return api.delete_form(tid); });
            return true;
        }
        return false;
    }
};

} // namespace

std::unique_ptr<Screen> make_forms(Ctx& ctx) {
    return std::make_unique<FormsScreen>(ctx);
}

} // namespace ui
