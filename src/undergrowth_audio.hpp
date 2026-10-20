#pragma once
#include "audio.hpp"
#include "undergrowth.hpp"

namespace viewer {
// Presentation only; an unavailable device/clip cannot suppress AI noise.
void playUndergrowth(AudioSystem& audio, const UndergrowthEvent& event);
}
