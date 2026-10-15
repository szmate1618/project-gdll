#pragma once

#include <glm/glm.hpp>

namespace viewer {

// Game tuning values, in dB at a reference distance of one meter.
inline constexpr float walkingFootstepDecibels = 40.0f;
inline constexpr float runningFootstepDecibels = 50.0f;
inline constexpr float landingDecibels = 55.0f;
inline constexpr float zombieHearingThresholdDecibels = 20.0f;

struct NoiseEvent {
    enum class Kind { generic, footstep, landing };
    glm::vec3 position{0}; // X east, Y up, Z south, in meters.
    float decibels = 0;    // Level at one meter from the source.
    Kind kind = Kind::generic; // Action metadata; never affects AI propagation.
};

// Simple distance-only propagation: source dB - 20 log10(distance / 1 m).
// Clamp distances below one meter; invalid input returns negative infinity.
float noiseLevelAt(const NoiseEvent& noise, glm::vec3 listener);

} // namespace viewer
