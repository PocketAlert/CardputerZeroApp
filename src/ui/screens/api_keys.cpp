#include "resource_screen.h"

#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

// ---------------------------------------------------------------- API keys

class ApiKeysScreen final : public ResourceScreen<pa::ApiKey> {
public:
    explicit ApiKeysScreen(Ctx& ctx)
        : ResourceScreen(ctx, tr("apiKeys.title"), 32, [](pa::Api& api) { return api.api_keys(); },
                         tr("apiKeys.empty.title"), "", icon::kKey) {}

protected:
    void setup() override { chrome_.set_keys({tr("apiKeys.keys.new"), "", "", "", tr("app.delete")}); }

    void build_row(lv_obj_t* row, const pa::ApiKey& key) override {
        const auto& t = theme();
        standard_row(row, icon::kKey, key.is_active ? t.p.accent : t.p.faint, key.name, tr("apiKeys.row.created", {{"date", key.created_at}}),
                     key.is_active ? "" : tr("apiKeys.row.inactive"), t.p.muted);
    }

    bool handle(const Key& key) override {
        if (key.slot() == 0 || key.is_char('n')) {
            ctx_.open_modal(prompt_modal(ctx_, tr("apiKeys.create.title"), tr("apiKeys.create.hint"), "", false,
                                         [this](const std::string& name) { create(pa::trim(name)); }));
            return true;
        }
        auto* item = selected();
        if (!item) return false;
        if (key.enter()) {
            ctx_.open_modal(info_modal(ctx_, item->name,
                                       tr("apiKeys.info.text", {{"tid", item->tid}, {"date", item->created_at}}),
                                       "", icon::kKey));
            return true;
        }
        if (key.slot() == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            const auto tid = item->tid;
            const char* warning = item->name == "Cardputer Zero" ? tr("apiKeys.confirm.deleteTextThisDevice")
                                                                 : tr("apiKeys.confirm.deleteText");
            confirm_remove(tr("apiKeys.confirm.deleteTitle", {{"name", item->name}}), warning,
                           [tid](pa::Api& api) { return api.delete_api_key(tid); });
            return true;
        }
        return false;
    }

private:
    void create(const std::string& name) {
        if (name.empty()) return;
        run_once<pa::Result<pa::ApiKey>>([name](pa::Api& api) { return api.create_api_key(name); },
                                    [this](pa::Result<pa::ApiKey>& result) {
                                        if (!result.ok) {
                                            ctx_.show_error(result.error, tr("apiKeys.toast.notCreated"));
                                            return;
                                        }
                                        reload();
                                        ctx_.open_modal(info_modal(ctx_, tr("apiKeys.created.title"),
                                                                   tr("apiKeys.created.text",
                                                                      {{"token", result.value.token}}),
                                                                   result.value.token, icon::kKey));
                                    });
    }
};

} // namespace

std::unique_ptr<Screen> make_api_keys(Ctx& ctx) {
    return std::make_unique<ApiKeysScreen>(ctx);
}

} // namespace ui
