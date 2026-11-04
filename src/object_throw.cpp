#include "object_throw.hpp"
#include "weapon_config.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace viewer {
glm::vec3 objectThrowImpulse(glm::vec3 direction, float massKg, float charge) {
    if (!std::isfinite(massKg) || massKg <= 0 || !std::isfinite(charge) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) return glm::vec3(0);
    constexpr float referenceMassKg = .45f;
    constexpr float effectiveArmMassKg = 3;
    constexpr float maxImpulseNs = 90; // Game strength limit for heavy props.
    const float speedScale = std::sqrt((referenceMassKg + effectiveArmMassKg) / (massKg + effectiveArmMassKg));
    const auto velocityGain = (direction * GrenadeConfig{}.throwSpeed + glm::vec3(0, 2, 0)) * speedScale;
    auto impulse = velocityGain * massKg;
    const float magnitude = glm::length(impulse);
    if (magnitude > maxImpulseNs) impulse *= maxImpulseNs / magnitude;
    return impulse * std::clamp(charge, 0.f, 1.f);
}
} // namespace viewer
