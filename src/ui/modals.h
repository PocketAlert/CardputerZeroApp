#pragma once

#include "ctx.h"
#include "key.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {

class Modal {
public:
    explicit Modal(Ctx& ctx);
    virtual ~Modal();
    Modal(const Modal&) = delete;
    Modal& operator=(const Modal&) = delete;

    virtual bool on_key(const Key& key) = 0;

    // The screen that was on top when it opened; closed together with it.
    const Screen* owner() const { return owner_; }
    void set_owner(const Screen* owner) { owner_ = owner; }

protected:
    // Dimmed overlay plus a titled panel; returns the panel's content area.
    lv_obj_t* make_panel(const std::string& title, const char* glyph, lv_color_t glyph_color, int height = 154);
    void set_footer(const std::string& text);
    void close();

    Ctx& ctx_;
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* footer_ = nullptr;

private:
    const Screen* owner_ = nullptr;
};

struct PickItem {
    std::string label;
    std::string hint;
    const char* glyph = nullptr;
    bool has_color = false;
    lv_color_t color{};
};

std::unique_ptr<Modal> confirm_modal(Ctx& ctx, const std::string& title, const std::string& text,
                                     const std::string& confirm_label, bool danger, std::function<void()> on_yes);
std::unique_ptr<Modal> info_modal(Ctx& ctx, const std::string& title, const std::string& text,
                                  const std::string& qr_payload = "", const char* glyph = nullptr);
std::unique_ptr<Modal> picker_modal(Ctx& ctx, const std::string& title, std::vector<PickItem> items, size_t selected,
                                    std::function<void(size_t)> on_pick);
std::unique_ptr<Modal> prompt_modal(Ctx& ctx, const std::string& title, const std::string& hint,
                                    const std::string& initial, bool secret,
                                    std::function<void(const std::string&)> on_submit);

} // namespace ui
