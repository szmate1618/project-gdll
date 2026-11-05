#include "weapon_accuracy.hpp"
#include "fps_controller.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>

namespace viewer {
float hipFireInaccuracy(const WeaponConfig& weapon, float speed, float posture,
                       const FPSConfig& movement, bool scoped) {
    if (scoped || weapon.grenade) return 0;
    speed = std::max(0.f, speed);
    posture = std::clamp(posture, 0.f, 2.f);
    const float baseline = weapon.hipFireInaccuracyDegrees.x;
    const float crouch = baseline * (movement.crouchSpeed > 0 ? std::clamp(speed / movement.crouchSpeed, 0.f, 1.f) : 0);
    const float prone = baseline * (movement.proneSpeed > 0 ? std::clamp(speed / movement.proneSpeed, 0.f, 1.f) : 0);
    if (posture >= 1) return glm::mix(crouch, prone, posture - 1);
    const float walk = movement.walkSpeed > 0 ? std::clamp(speed / movement.walkSpeed, 0.f, 1.f) : 0;
    const float run = std::clamp((speed - movement.walkSpeed) / (movement.runSpeed - movement.walkSpeed), 0.f, 1.f);
    const float standing = glm::mix(baseline, weapon.hipFireInaccuracyDegrees.y, walk) +
        (weapon.hipFireInaccuracyDegrees.z - weapon.hipFireInaccuracyDegrees.y) * run;
    return glm::mix(standing, crouch, posture);
}

glm::vec2 ShotInaccuracy::sample(float standardDeviationDegrees) {
    if (!std::isfinite(standardDeviationDegrees) || standardDeviationDegrees < 0)
        throw std::invalid_argument("Shot inaccuracy must be finite and nonnegative");
    if (standardDeviationDegrees == 0) return {0, 0};
    const float yaw = normal_(random_), pitch = normal_(random_);
    return standardDeviationDegrees * glm::vec2(yaw, pitch);
}

glm::vec3 inaccurateShotDirection(glm::vec3 forward, glm::vec2 offsetDegrees) {
    forward = glm::normalize(forward);
    if (offsetDegrees == glm::vec2(0)) return forward;
    const auto reference = std::abs(forward.y) > .99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const auto right = glm::normalize(glm::cross(forward, reference));
    const auto up = glm::cross(right, forward);
    const auto yaw = glm::angleAxis(glm::radians(-offsetDegrees.x), up);
    const auto pitch = glm::angleAxis(glm::radians(offsetDegrees.y), right);
    return glm::normalize(yaw * pitch * forward);
}

float inaccuracyCrosshairRadius(float standardDeviationDegrees, float projectionY) {
    // For two independent normal components: P(radius <= r) = 1-exp(-r²/2σ²).
    constexpr float radius95 = 2.44774683f;
    return std::tan(glm::radians(radius95 * standardDeviationDegrees)) * projectionY;
}
} // namespace viewer
