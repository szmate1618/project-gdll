#include "fps_controller.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

float FPSController::movementSpeed(bool run, float forward, float right) const {
    const auto movingStance = grounded_ ? stance_ : takeoffStance_;
    if (movingStance == PlayerStance::Prone) return config_.proneSpeed;
    if (movingStance == PlayerStance::Crouched) return config_.crouchSpeed;
    if (!run) return config_.walkSpeed;
    const float longitudinal = forward < 0 ? (config_.runSpeed + config_.walkSpeed) * .5f : config_.runSpeed;
    const float sideways = (3 * config_.runSpeed + config_.walkSpeed) * .25f;
    const float weight = std::abs(forward) + std::abs(right);
    return weight > 0 ? (std::abs(forward) * longitudinal + std::abs(right) * sideways) / weight : 0;
}

glm::vec3 FPSController::updateHorizontalMovement(float dt, const FPSInput& input) {
    const float yaw = glm::radians(input.yawDegrees);
    const glm::vec2 forward(std::cos(yaw), std::sin(yaw));
    const glm::vec2 right(-forward.y, forward.x);
    glm::vec2 target = forward * input.forward + right * input.right;
    const float length = glm::length(target);
    if (length > 1) target /= length;
    target *= movementSpeed(input.run, input.forward, input.right);

    const glm::vec2 delta = target - horizontalVelocity_;
    const float distance = glm::length(delta);
    if (distance > 0) {
        const auto direction = delta / distance;
        // One shared acceleration in m/s²: forward walk -> run takes 2.5 s.
        const float acceleration = (config_.runSpeed - config_.walkSpeed) / 2.5f;
        // Along the velocity change, speed decreases up to the closest point
        // to zero. Split there so reversing brakes at 2a, then speeds up at a.
        const float brakingDistance = std::clamp(-glm::dot(horizontalVelocity_, direction), 0.f, distance);
        const float brakingTravel = std::min(brakingDistance, 2 * acceleration * dt);
        const float remaining = std::max(0.f, dt - brakingTravel / (2 * acceleration));
        const float travel = brakingTravel + acceleration * remaining;
        if (travel >= distance) horizontalVelocity_ = target;
        else horizontalVelocity_ += direction * travel;
    }
    return {horizontalVelocity_.x, 0, horizontalVelocity_.y};
}

} // namespace viewer
