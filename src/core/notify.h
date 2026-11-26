#pragma once

#include "config.h"
#include "models.h"

#include <string>
#include <vector>

namespace pa {

constexpr size_t kSeenHistory = 300;

struct Detection {
    std::vector<Message> fresh;
    int unread = 0;
};

// TIDs are random, so "new" means "not seen before", not "newer than X".
// The first run only records what is already there, so installing the app
// does not replay the whole inbox as alerts.
Detection detect_new(const std::vector<Message>& latest, NotifyState& state);

int loudest_level(const std::vector<Message>& messages);

enum class Sound { Alert, Critical, Click, Sent };

// Fire-and-forget playback through the system player (aplay on the device,
// afplay in the macOS simulator). Never blocks the caller.
void play_sound(Sound sound);
void set_sounds_dir(std::string dir);

} // namespace pa
