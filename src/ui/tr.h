#pragma once

#include "i18n.h"

#include <string>

namespace ui {

using pa::i18n::tr;

// Display name of a message level; pa::level_name() stays English for the API and CLI specs.
inline std::string level_label(int level) {
    switch (level) {
        case -2: return tr("app.level.silent");
        case -1: return tr("app.level.low");
        case 0: return tr("app.level.default");
        case 1: return tr("app.level.high");
        case 2: return tr("app.level.critical");
        default: return std::to_string(level);
    }
}

} // namespace ui
