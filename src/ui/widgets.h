#pragma once

#include "key.h"
#include "theme.h"

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ui {

lv_obj_t* make_box(lv_obj_t* parent);
lv_obj_t* make_label(lv_obj_t* parent, const std::string& text, const lv_font_t* font, lv_color_t color);
lv_obj_t* make_icon(lv_obj_t* parent, const char* glyph, lv_color_t color, const lv_font_t* font = nullptr);
lv_obj_t* make_textarea(lv_obj_t* parent, const std::string& text, bool secret, bool one_line);
lv_obj_t* make_qr(lv_obj_t* parent, const std::string& payload, int size);
void set_row_flex(lv_obj_t* obj, int gap);
// Clips to one line with an ellipsis; without a fixed height LVGL would wrap instead.
void single_line(lv_obj_t* label);
void set_column_flex(lv_obj_t* obj, int gap);

// Applies one key to a textarea the way a terminal line editor would.
// Returns false for keys it does not own (Enter, Esc, arrows up/down, ...).
bool edit_text(lv_obj_t* textarea, const Key& key);

std::string short_time(const std::string& created_at);
std::string one_line(const std::string& text, size_t max_bytes = 160);
std::string human_size(long long bytes);

struct StatusInfo {
    int unread = 0;
    bool unread_overflow = false;
    bool offline = false;
    bool wifi_present = false;
    bool wifi_connected = false;
    int battery = -1;
    bool charging = false;
    bool demo = false;
    std::string clock;

    bool operator==(const StatusInfo& o) const {
        return unread == o.unread && unread_overflow == o.unread_overflow && offline == o.offline &&
               wifi_present == o.wifi_present && wifi_connected == o.wifi_connected && battery == o.battery &&
               charging == o.charging && demo == o.demo && clock == o.clock;
    }
    bool operator!=(const StatusInfo& o) const { return !(*this == o); }
};

class Chrome {
public:
    void create(lv_obj_t* root, const std::string& title);
    void set_title(const std::string& title);
    void set_subtitle(const std::string& text, lv_color_t color);
    void clear_subtitle();
    // Labels for keys 4..8; an empty string leaves the slot blank.
    void set_keys(const std::array<std::string, 5>& labels);
    // Rebuilds the status icons only when something in them changed.
    void set_status(const StatusInfo& status);
    void set_busy(bool busy);
    lv_obj_t* body() const { return body_; }

private:
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* busy_ = nullptr;
    lv_obj_t* body_ = nullptr;
    std::array<lv_obj_t*, 5> slots_{};
    StatusInfo shown_;
    bool has_status_ = false;
};

class ListView {
public:
    using Builder = std::function<void(lv_obj_t* row, size_t index)>;

    void create(lv_obj_t* parent, int row_height);
    void set_items(size_t count, Builder builder, size_t keep_selected = 0);
    // Cheaper than set_items when the existing rows stay valid: rows for the
    // new last items, or for `added` new first items (the selection follows
    // its row). Fall back to set_items while the empty state shows.
    void append_items(size_t count);
    void prepend_items(size_t added);
    void set_empty(const std::string& title, const std::string& hint, const char* glyph);
    void rebuild_row(size_t index);

    size_t count() const { return rows_.size(); }
    size_t selected() const { return selected_; }
    bool empty() const { return rows_.empty(); }
    void select(size_t index);
    bool handle_key(const Key& key);

    std::function<void()> on_end_reached;
    std::function<void(size_t)> on_selection;

private:
    void style_row(size_t index, bool selected);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* empty_ = nullptr;
    std::vector<lv_obj_t*> rows_;
    Builder builder_;
    size_t selected_ = 0;
    int row_height_ = 30;
    std::string empty_title_;
    std::string empty_hint_;
    const char* empty_icon_ = nullptr;
};

struct Field {
    enum class Kind { Text, Secret, Choice, Toggle, Button, Note };

    std::string id;
    std::string label;
    Kind kind = Kind::Text;
    std::string value;
    std::vector<std::pair<std::string, std::string>> options;
    bool on = false;
    std::string placeholder;
    bool visible = true;
    // Opens a gap above the row to set it apart from the fields before it.
    bool divider_above = false;
    std::function<void()> action;

    std::string choice_label() const;
};

class FormView {
public:
    void create(lv_obj_t* parent);
    void set_fields(std::vector<Field> fields);
    Field* find(const std::string& id);
    std::string value(const std::string& id) const;
    bool toggled(const std::string& id) const;
    void set_value(const std::string& id, const std::string& value);
    void set_options(const std::string& id, std::vector<std::pair<std::string, std::string>> options);
    void set_visible(const std::string& id, bool visible);
    void focus(const std::string& id);
    void render();

    bool handle_key(const Key& key);
    bool editing() const { return editor_ != nullptr; }
    void finish_editing() { end_edit(true); }

    std::function<void(const std::string& id)> on_change;
    // Enter on a Choice field: the owning screen opens a picker and calls apply.
    std::function<void(const Field& field, std::function<void(const std::string&)> apply)> on_choose;

private:
    void fill_row(size_t index);
    void refresh_selection(size_t previous);
    void begin_edit();
    void end_edit(bool commit);
    void move(int direction);
    void cycle(int direction);
    void ensure_selectable(int direction);
    bool selectable(size_t index) const;

    lv_obj_t* container_ = nullptr;
    std::vector<Field> fields_;
    std::vector<lv_obj_t*> rows_;
    std::vector<lv_obj_t*> value_labels_;
    size_t selected_ = 0;
    lv_obj_t* editor_ = nullptr;
};

} // namespace ui
