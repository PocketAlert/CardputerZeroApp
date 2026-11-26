#include "screens.h"

#include "modals.h"
#include "parse.h"
#include "tr.h"

#include <algorithm>

namespace ui {
namespace {

// Small pages: every row is decrypted server-side and travels over the
// device's Wi-Fi, so more is fetched only as the selection nears the end.
constexpr int kPageSize = 25;

class InboxScreen final : public Screen {
public:
    InboxScreen(Ctx& ctx, InboxFilter filter)
        : Screen(ctx, tr("inbox.title")), filter_(std::move(filter)), opened_filtered_(!filter_.app_tid.empty()) {}

    bool is_root() const override { return !opened_filtered_; }

    void build() override {
        chrome_.set_keys({tr("inbox.keys.new"), tr("inbox.keys.filter"), tr("inbox.keys.search"), tr("inbox.keys.more"),
                          tr("inbox.keys.menu")});
        list_.create(chrome_.body(), 34);
        list_.set_empty(tr("app.loading"), "", icon::kTray);
        list_.on_end_reached = [this] {
            if (has_more_ && !loading_) load(false);
        };
        list_.on_selection = [this](size_t index) {
            if (index < visible_.size() && visible_[index] < messages_.size()) {
                selected_tid_ = messages_[visible_[index]].tid;
            }
        };
        update_title();
        render();
        changes_cursor_ = ctx_.message_changes_end();
        load(true);
    }

    // Edits made on the message screen are patched in locally; the list is
    // not downloaded again just because the user came back to it.
    void on_resume() override {
        std::vector<Ctx::MessageChange> changes;
        if (!ctx_.take_message_changes(changes_cursor_, changes)) {
            load(true);
            return;
        }
        bool touched = false;
        for (const auto& change : changes) {
            const auto it = std::find_if(messages_.begin(), messages_.end(),
                                         [&](const pa::Message& m) { return m.tid == change.tid; });
            if (it == messages_.end()) continue;
            if (change.removed) {
                messages_.erase(it);
                if (total_ > 0) --total_;
                if (next_offset_ > 0) --next_offset_;
            } else {
                it->is_read = change.is_read;
            }
            touched = true;
        }
        if (touched) render();
    }

    // The poll already carries the new messages; prepend them instead of refetching.
    void on_new_messages(const std::vector<pa::Message>& fresh) override {
        if (!filter_.app_tid.empty()) {
            load(true);
            return;
        }
        std::vector<pa::Message> added;
        for (const auto& m : fresh) {
            const bool known = std::any_of(messages_.begin(), messages_.end(),
                                           [&](const pa::Message& x) { return x.tid == m.tid; });
            if (!known) added.push_back(m);
        }
        if (added.empty()) return;
        total_ += static_cast<int>(added.size());
        next_offset_ += static_cast<int>(added.size());
        messages_.insert(messages_.begin(), added.begin(), added.end());
        // Every old index shifts by the same amount, so the rows already on
        // screen stay valid and only the new ones are built.
        const auto old_visible = visible_.size();
        visible_ = pa::filter_messages(messages_, unread_only_, search_);
        list_.prepend_items(visible_.size() - old_visible);
        update_title();
    }

    bool on_key(const Key& key) override {
        if (list_.handle_key(key)) return true;
        if (key.enter()) {
            open_selected();
            return true;
        }
        switch (key.slot()) {
            case 0: ctx_.push(make_compose(ctx_, ComposePreset{filter_.app_tid, ""})); return true;
            case 1: choose_filter(); return true;
            case 2: ask_search(); return true;
            case 3: more(); return true;
            case 4: ctx_.push(make_menu(ctx_)); return true;
            default: break;
        }
        if (key.is_char('n')) { ctx_.push(make_compose(ctx_, ComposePreset{filter_.app_tid, ""})); return true; }
        if (key.is_char('r')) { toggle_read(); return true; }
        if (key.is_char('d') || key.backspace() || key.is(LV_KEY_DEL)) { delete_selected(); return true; }
        if (key.is_char('u')) { unread_only_ = !unread_only_; render(); return true; }
        if (key.is_char('s')) { ask_search(); return true; }
        if (key.is_char('g')) { load(true); return true; }
        if (key.esc() && (!search_.empty() || unread_only_)) {
            search_.clear();
            unread_only_ = false;
            render();
            return true;
        }
        if (key.esc() && !opened_filtered_ && list_.selected() > 0) {
            list_.select(0);
            return true;
        }
        if (key.esc() && !opened_filtered_ && !filter_.app_tid.empty()) {
            filter_ = {};
            messages_.clear();
            render();
            load(true);
            return true;
        }
        return false;
    }

    std::string help() const override {
        return tr("inbox.help");
    }

private:
    void update_title() {
        chrome_.set_title(filter_.app_name.empty() ? std::string(tr("inbox.title")) : filter_.app_name);
        std::string tags;
        if (unread_only_) tags += tr("inbox.subtitle.unread");
        if (!search_.empty()) tags += (tags.empty() ? "" : " · ") + tr("inbox.subtitle.search", {{"query", search_}});
        if (!tags.empty()) {
            chrome_.set_subtitle(tags, theme().p.accent);
        } else if (total_ > 0) {
            chrome_.set_subtitle(std::to_string(total_), theme().p.muted);
        } else {
            chrome_.clear_subtitle();
        }
    }

    void load(bool reset) {
        if (loading_) {
            reload_pending_ = reload_pending_ || reset;
            return;
        }
        loading_ = true;
        pa::MessageQuery query;
        query.size = kPageSize;
        query.offset = reset ? 0 : next_offset_;
        query.application_tid = filter_.app_tid;
        run<pa::Result<pa::MessagePage>>(
            [query](pa::Api& api) { return api.messages(query); },
            [this, reset](pa::Result<pa::MessagePage>& result) {
                loading_ = false;
                if (!result.ok) {
                    if (messages_.empty()) {
                        list_.set_empty(tr("inbox.empty.loadFailed"), result.error.text(), icon::kWifiOff);
                        render();
                    }
                    ctx_.show_error(result.error);
                } else {
                    has_more_ = result.value.has_more;
                    total_ = result.value.total;
                    list_.set_empty(tr(filter_.app_tid.empty() ? "inbox.empty.title" : "inbox.empty.filteredTitle"),
                                    tr("inbox.empty.hint"), icon::kTray);
                    if (reset) {
                        messages_ = std::move(result.value.data);
                        render();
                    } else {
                        // A page older than what is loaded: build only its rows.
                        for (auto& m : result.value.data) messages_.push_back(std::move(m));
                        visible_ = pa::filter_messages(messages_, unread_only_, search_);
                        list_.append_items(visible_.size());
                        update_title();
                    }
                    next_offset_ = has_more_ ? result.value.next_offset : static_cast<int>(messages_.size());
                }
                if (reload_pending_) {
                    reload_pending_ = false;
                    load(true);
                }
            });
    }

    // Rebuilds the list from messages_. The selection is remembered by tid in
    // on_selection, never read back through visible_: by the time render()
    // runs, messages_ may already have shrunk under the old indices.
    void render() {
        visible_ = pa::filter_messages(messages_, unread_only_, search_);
        const auto& selected_tid = selected_tid_;
        size_t keep = 0;
        for (size_t i = 0; i < visible_.size(); ++i) {
            if (messages_[visible_[i]].tid == selected_tid) keep = i;
        }
        list_.set_items(visible_.size(), [this](lv_obj_t* row, size_t i) { build_row(row, messages_[visible_[i]]); },
                        keep);
        remember_selection();
        update_title();
    }

    void remember_selection() {
        if (list_.selected() < visible_.size() && visible_[list_.selected()] < messages_.size()) {
            selected_tid_ = messages_[visible_[list_.selected()]].tid;
        }
    }

    void build_row(lv_obj_t* row, const pa::Message& m) {
        const auto& t = theme();
        if (m.priority != 0) {
            auto* stripe = make_box(row);
            lv_obj_set_size(stripe, 3, 26);
            lv_obj_set_pos(stripe, -5, 4);
            lv_obj_set_style_radius(stripe, 1, 0);
            lv_obj_set_style_bg_color(stripe, level_color(m.priority), 0);
            lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        }
        if (!m.is_read) {
            auto* dot = make_box(row);
            lv_obj_set_size(dot, 6, 6);
            lv_obj_set_pos(dot, 2, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, t.p.accent, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        }

        auto* time = make_label(row, short_time(m.created_at), t.f.small, m.is_read ? t.p.faint : t.p.muted);
        lv_obj_align(time, LV_ALIGN_TOP_RIGHT, 0, 3);

        int extras = 0;
        if (!m.actions.empty()) {
            auto* bolt = make_icon(row, icon::kLightning, t.p.faint, t.f.icon_small);
            lv_obj_align_to(bolt, time, LV_ALIGN_OUT_LEFT_MID, -4, 0);
            ++extras;
        }
        if (m.attachment) {
            auto* clip = make_icon(row, icon::kPaperclip, t.p.faint, t.f.icon_small);
            lv_obj_align_to(clip, time, LV_ALIGN_OUT_LEFT_MID, -4 - extras * 14, 0);
            ++extras;
        }

        auto* title = make_label(row, one_line(m.title, 120), m.is_read ? t.f.body : t.f.body_bold,
                                 m.is_read ? t.p.muted : t.p.text);
        lv_obj_set_pos(title, 12, 2);
        lv_obj_set_width(title, 238 - extras * 14);
        single_line(title);

        auto* line = make_box(row);
        lv_obj_set_pos(line, 12, 18);
        lv_obj_set_size(line, 294, 14);
        set_row_flex(line, 5);
        if (!m.application.empty()) {
            const auto color = ctx_.app_color(m.application);
            auto* app = make_label(line, m.application, t.f.small_bold,
                                   color.empty() ? t.p.muted : parse_hex_color(color, t.p.muted));
            lv_obj_set_style_max_width(app, 96, 0);
            single_line(app);
        }
        auto* preview = make_label(line, one_line(m.message, 140), t.f.small, t.p.faint);
        lv_obj_set_flex_grow(preview, 1);
        single_line(preview);
    }

    pa::Message* selected_message() {
        if (list_.empty() || list_.selected() >= visible_.size() || visible_[list_.selected()] >= messages_.size()) {
            return nullptr;
        }
        return &messages_[visible_[list_.selected()]];
    }

    void open_selected() {
        auto* m = selected_message();
        if (!m) return;
        if (!m->is_read) {
            m->is_read = true;
            list_.rebuild_row(list_.selected());
        }
        ctx_.push(make_message(ctx_, *m));
    }

    // Optimistic: the row flips at once and flips back if the server refuses.
    void toggle_read() {
        auto* m = selected_message();
        if (!m) return;
        const bool read = !m->is_read;
        const auto tid = m->tid;
        set_read_locally(tid, read);
        run<pa::Result<pa::Empty>>([tid, read](pa::Api& api) { return api.set_read(tid, read); },
                                   [this, tid, read](pa::Result<pa::Empty>& result) {
                                       if (!result.ok) {
                                           set_read_locally(tid, !read);
                                           ctx_.show_error(result.error);
                                           return;
                                       }
                                       ctx_.note_message_change({tid, false, read});
                                       changes_cursor_ = ctx_.message_changes_end();
                                   });
    }

    void set_read_locally(const std::string& tid, bool read) {
        for (size_t row = 0; row < visible_.size(); ++row) {
            auto& m = messages_[visible_[row]];
            if (m.tid != tid) continue;
            m.is_read = read;
            if (unread_only_) {
                render();
            } else {
                list_.rebuild_row(row);
            }
            return;
        }
    }

    void delete_selected() {
        auto* m = selected_message();
        if (!m) return;
        const auto tid = m->tid;
        ctx_.open_modal(confirm_modal(ctx_, tr("inbox.confirm.delete.title"), one_line(m->title, 90), tr("app.delete"), true, [this, tid] {
            run_once<pa::Result<pa::Empty>>([tid](pa::Api& api) { return api.delete_message(tid); },
                                       [this, tid](pa::Result<pa::Empty>& result) {
                                           if (!result.ok) {
                                               ctx_.show_error(result.error);
                                               return;
                                           }
                                           messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                                                          [&](const pa::Message& x) { return x.tid == tid; }),
                                                           messages_.end());
                                           if (total_ > 0) --total_;
                                           if (next_offset_ > 0) --next_offset_;
                                           render();
                                           ctx_.toast(tr("inbox.toast.deleted"), ToastKind::Success);
                                       });
        }));
    }

    void choose_filter() {
        std::vector<PickItem> items;
        items.push_back({tr("inbox.filter.allApps"), "", icon::kTray});
        size_t selected = 0;
        for (size_t i = 0; i < ctx_.apps().size(); ++i) {
            const auto& app = ctx_.apps()[i];
            PickItem item{app.name, "", nullptr, true, parse_hex_color(app.color, theme().p.muted)};
            items.push_back(item);
            if (app.tid == filter_.app_tid) selected = i + 1;
        }
        ctx_.open_modal(picker_modal(ctx_, tr("inbox.filter.title"), std::move(items), selected, [this](size_t index) {
            if (index == 0) {
                filter_ = {};
            } else {
                const auto& app = ctx_.apps()[index - 1];
                filter_ = {app.tid, app.name};
            }
            messages_.clear();
            render();
            load(true);
        }));
    }

    void ask_search() {
        ctx_.open_modal(prompt_modal(ctx_, tr("inbox.search.title"), tr("inbox.search.hint"), search_,
                                     false, [this](const std::string& text) {
                                         search_ = pa::trim(text);
                                         render();
                                     }));
    }

    void more() {
        auto* m = selected_message();
        std::vector<PickItem> items;
        std::vector<std::function<void()>> actions;
        if (m) {
            items.push_back({tr(m->is_read ? "inbox.more.markUnread" : "inbox.more.markRead"), "R", icon::kEnvelopeOpen});
            actions.push_back([this] { toggle_read(); });
            items.push_back({tr("inbox.more.delete"), "D", icon::kTrash});
            actions.push_back([this] { delete_selected(); });
        }
        items.push_back({tr(unread_only_ ? "inbox.more.showAll" : "inbox.more.showUnread"), "U", icon::kFunnel});
        actions.push_back([this] {
            unread_only_ = !unread_only_;
            render();
        });
        items.push_back({tr("inbox.more.markAllRead"), "", icon::kCheckCircle});
        actions.push_back([this] { mark_all_read(); });
        items.push_back({tr("inbox.more.deleteAll"), "", icon::kTrash});
        actions.push_back([this] { delete_all(); });
        items.push_back({tr("inbox.more.refresh"), "G", icon::kRefresh});
        actions.push_back([this] { load(true); });
        ctx_.open_modal(picker_modal(ctx_, tr("inbox.more.title"), std::move(items), 0, [actions](size_t index) { actions[index](); }));
    }

    void mark_all_read() {
        run_once<pa::Result<pa::Empty>>([](pa::Api& api) { return api.mark_all_read(); },
                                   [this](pa::Result<pa::Empty>& result) {
                                       if (!result.ok) {
                                           ctx_.show_error(result.error);
                                           return;
                                       }
                                       for (auto& m : messages_) m.is_read = true;
                                       render();
                                       ctx_.toast(tr("inbox.toast.allRead"), ToastKind::Success);
                                       ctx_.poll_now();
                                   });
    }

    void delete_all() {
        ctx_.open_modal(confirm_modal(ctx_, tr("inbox.confirm.deleteAll.title"), tr("inbox.confirm.deleteAll.text"),
                                      tr("inbox.confirm.deleteAll.button"), true, [this] {
                                          run_once<pa::Result<pa::Empty>>(
                                              [](pa::Api& api) { return api.delete_all_messages(); },
                                              [this](pa::Result<pa::Empty>& result) {
                                                  if (!result.ok) {
                                                      ctx_.show_error(result.error);
                                                      return;
                                                  }
                                                  messages_.clear();
                                                  total_ = 0;
                                                  next_offset_ = 0;
                                                  has_more_ = false;
                                                  render();
                                                  ctx_.toast(tr("inbox.toast.cleared"), ToastKind::Success);
                                                  ctx_.poll_now();
                                              });
                                      }));
    }

    InboxFilter filter_;
    bool opened_filtered_ = false;
    ListView list_;
    std::vector<pa::Message> messages_;
    std::vector<size_t> visible_;
    std::string search_;
    bool unread_only_ = false;
    bool loading_ = false;
    bool reload_pending_ = false;
    bool has_more_ = false;
    int next_offset_ = 0;
    int total_ = 0;
    size_t changes_cursor_ = 0;
    std::string selected_tid_;
};

} // namespace

std::unique_ptr<Screen> make_inbox(Ctx& ctx, InboxFilter filter) {
    return std::make_unique<InboxScreen>(ctx, std::move(filter));
}

} // namespace ui
