#include "resource_screen.h"

#include "parse.h"
#include "tr.h"

namespace ui {
namespace {

// ---------------------------------------------------------------- devices

class DevicesScreen final : public ResourceScreen<pa::Device> {
public:
    explicit DevicesScreen(Ctx& ctx)
        : ResourceScreen(ctx, tr("devices.title"), 32, [](pa::Api& api) { return api.devices(); },
                         tr("devices.empty.title"), tr("devices.empty.hint"), icon::kDevice) {}

protected:
    void setup() override {
        chrome_.set_keys({tr("devices.keys.rename"), tr("devices.keys.send"), "", tr("devices.keys.info"),
                          tr("app.delete")});
    }

    void build_row(lv_obj_t* row, const pa::Device& device) override {
        const auto& t = theme();
        standard_row(row, icon::kDevice, device.is_active ? t.p.accent : t.p.faint, device.name, device.serial_number,
                     device.is_active ? "" : tr("devices.row.inactive"), t.p.muted);
    }

    bool handle(const Key& key) override {
        auto* device = selected();
        if (!device) return false;
        const int slot = key.slot();
        if (slot == 0 || key.is_char('r')) {
            const auto tid = device->tid;
            ctx_.open_modal(prompt_modal(ctx_, tr("devices.rename.title"), "", device->name, false, [this, tid](const std::string& name) {
                if (pa::trim(name).empty()) return;
                run_once<pa::Result<pa::Empty>>([tid, name](pa::Api& api) { return api.rename_device(tid, pa::trim(name)); },
                                           [this](pa::Result<pa::Empty>& result) {
                                               if (!result.ok) {
                                                   ctx_.show_error(result.error);
                                                   return;
                                               }
                                               ctx_.toast(tr("devices.toast.renamed"), ToastKind::Success);
                                               ctx_.load_lookups();
                                               reload();
                                           });
            }));
            return true;
        }
        if (slot == 1) {
            ctx_.push(make_compose(ctx_, ComposePreset{"", device->tid}));
            return true;
        }
        if (slot == 3 || key.enter()) {
            ctx_.open_modal(info_modal(ctx_, device->name,
                                       tr("devices.info.text", {{"serial", device->serial_number},
                                                                {"tid", device->tid},
                                                                {"active", device->is_active ? tr("devices.info.yes")
                                                                                             : tr("devices.info.no")},
                                                                {"added", device->created_at}}),
                                       "", icon::kDevice));
            return true;
        }
        if (slot == 4 || key.is_char('d') || key.is(LV_KEY_DEL)) {
            const auto tid = device->tid;
            confirm_remove(tr("devices.confirm.removeTitle", {{"name", device->name}}), tr("devices.confirm.removeText"),
                           [tid](pa::Api& api) { return api.delete_device(tid); }, [this] { ctx_.load_lookups(); });
            return true;
        }
        return false;
    }
};

} // namespace

std::unique_ptr<Screen> make_devices(Ctx& ctx) {
    return std::make_unique<DevicesScreen>(ctx);
}

} // namespace ui
