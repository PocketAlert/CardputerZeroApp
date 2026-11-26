#pragma once

#include "ctx.h"
#include "key.h"
#include "widgets.h"

#include <memory>
#include <string>

namespace ui {

class Screen {
public:
    Screen(Ctx& ctx, std::string title);
    virtual ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    void create();
    lv_obj_t* root() const { return root_; }
    Chrome& chrome() { return chrome_; }

    virtual void build() = 0;
    virtual bool on_key(const Key& key) = 0;
    virtual void on_resume() {}
    virtual void on_new_messages(const std::vector<pa::Message>&) {}
    virtual std::string help() const;
    virtual bool is_root() const { return false; }

protected:
    template <typename T>
    void run(std::function<T(pa::Api&)> job, std::function<void(T&)> done) {
        ctx_.run<T>(alive_, std::move(job), std::move(done));
    }
    // For actions that change data: a second press while the first request
    // is in flight is ignored instead of creating or deleting twice.
    template <typename T>
    void run_once(std::function<T(pa::Api&)> job, std::function<void(T&)> done) {
        if (submitting_) return;
        submitting_ = true;
        run<T>(std::move(job), [this, done = std::move(done)](T& result) {
            submitting_ = false;
            done(result);
        });
    }
    bool submitting() const { return submitting_; }
    // Leaves this screen, if it is still the one on top.
    void close_self() { ctx_.pop(this); }
    void attach(FormView& form, const std::string& title_prefix = "");

    Ctx& ctx_;
    std::string title_;
    lv_obj_t* root_ = nullptr;
    Chrome chrome_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

private:
    bool submitting_ = false;
};

} // namespace ui
