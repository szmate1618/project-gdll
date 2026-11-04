#pragma once

#include <glm/vec3.hpp>

namespace viewer {
// Added world-space momentum in N*s, for a normalized aim direction, object
// mass in kg and charge [0, 1]. Apply as an impulse; retain existing motion.
// Project approximation and research background: ATTRIBUTION.md,
// "Gameplay research references". Parameters are game tuning, not study fits.
glm::vec3 objectThrowImpulse(glm::vec3 direction, float massKg, float charge);
} // namespace viewer
