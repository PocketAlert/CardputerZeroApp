#include "screen.h"

#include "modals.h"

namespace ui {

Screen::Screen(Ctx& ctx, std::string title) : ctx_(ctx), title_(std::move(title)) {}

Screen::~Screen() {
    alive_.reset();
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

void Screen::create() {
    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, kScreenW, kScreenH);
    chrome_.create(root_, title_);
    build();
    ctx_.refresh_chrome(this);
}

std::string Screen::help() const {
    return {};
}

void Screen::attach(FormView& form, const std::string& title_prefix) {
    form.on_choose = [this, title_prefix](const Field& field, std::function<void(const std::string&)> apply) {
        std::vector<PickItem> items;
        std::vector<std::string> values;
        size_t selected = 0;
        for (size_t i = 0; i < field.options.size(); ++i) {
            const auto& [value, label] = field.options[i];
            PickItem item;
            item.label = label;
            if (field.id == "color") {
                item.has_color = true;
                item.color = parse_hex_color(value, theme().p.muted);
            } else if (field.id == "app" || field.id == "application") {
                const auto color = ctx_.app_color(label);
                if (!color.empty()) {
                    item.has_color = true;
                    item.color = parse_hex_color(color, theme().p.muted);
                }
            }
            items.push_back(item);
            values.push_back(value);
            if (value == field.value) selected = i;
        }
        ctx_.open_modal(picker_modal(ctx_, title_prefix + field.label, std::move(items), selected,
                                     [apply, values](size_t index) { apply(values[index]); }));
    };
}

} // namespace ui
