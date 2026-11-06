#pragma once

#include <glm/glm.hpp>
#include <optional>

namespace viewer {

// Game tuning values, in dB at a reference distance of one meter.
inline constexpr float walkingFootstepDecibels = 40.0f;
inline constexpr float runningFootstepDecibels = 50.0f;
inline constexpr float landingDecibels = 55.0f;
inline constexpr float zombieHearingThresholdDecibels = 20.0f;
inline constexpr float doorBreakDecibels = 75.0f;

enum class NoiseAudience { everyone, playersOnly };

struct NoiseEvent {
    enum class Kind { generic, footstep, landing, rustle, dryRustle, twigSnap, doorSlam, doorBreak, zombieImpact };
    glm::vec3 position{0}; // X east, Y up, Z south, in meters.
    float decibels = 0;    // Level at one meter from the source.
    Kind kind = Kind::generic; // Action metadata; never affects AI propagation.
    NoiseAudience audience = NoiseAudience::everyone;
};

// Simple distance-only propagation: source dB - 20 log10(distance / 1 m).
// Clamp distances below one meter; invalid input returns negative infinity.
float noiseLevelAt(const NoiseEvent& noise, glm::vec3 listener);
// Contact only, not an attack windup. Quiet motion below 0.8 m/s emits nothing;
// 2 m/s gives 55 dB, capped at 70 dB. Never attracts zombies or dogs.
std::optional<NoiseEvent> zombieImpactNoise(glm::vec3 position, float speed);

} // namespace viewer
