#pragma once
#include "graphics_settings.hpp"

namespace viewer {
struct AudioSettings {
    float volume = 0.6f; // Master gain, 0 (mute) to 1 (full volume).
};
struct Settings {
    GraphicsSettings graphics;
    AudioSettings audio;
};
}
