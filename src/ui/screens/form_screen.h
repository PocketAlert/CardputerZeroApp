#pragma once

#include "screen.h"

namespace ui {

// A screen built around one form: key 4 submits, key 8 leaves, everything
// else goes to the form. Typing in a field never triggers those shortcuts.
class FormScreen : public Screen {
public:
    using Screen::Screen;

    bool on_key(const Key& key) override {
        if (!form_.editing()) {
            if (key.slot() == 0) {
                submit();
                return true;
            }
            if (key.slot() == 4) {
                close_self();
                return true;
            }
            if (on_shortcut(key)) return true;
        }
        return form_.handle_key(key);
    }

protected:
    virtual void submit() = 0;
    // Extra bottom-bar keys of a particular form.
    virtual bool on_shortcut(const Key&) { return false; }

    FormView form_;
};

} // namespace ui
