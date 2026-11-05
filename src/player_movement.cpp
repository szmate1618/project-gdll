#include "fps_controller.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

float FPSController::baseMovementSpeed(bool run, float forward, float right) const {
    const auto movingStance = grounded_ ? stance_ : takeoffStance_;
    if (movingStance == PlayerStance::Prone) return config_.proneSpeed;
    if (movingStance == PlayerStance::Crouched) return config_.crouchSpeed;
    if (!run) return config_.walkSpeed;
    const float longitudinal = forward < 0 ? (config_.runSpeed + config_.walkSpeed) * .5f : config_.runSpeed;
    const float sideways = (3 * config_.runSpeed + config_.walkSpeed) * .25f;
    const float weight = std::abs(forward) + std::abs(right);
    return weight > 0 ? (std::abs(forward) * longitudinal + std::abs(right) * sideways) / weight : 0;
}

float FPSController::movementSpeed(bool run, float forward, float right) const {
    const auto movingStance = grounded_ ? stance_ : takeoffStance_;
    const float multiplier = run && movingStance == PlayerStance::Standing ? exertion_.speedMultiplier() : 1;
    return baseMovementSpeed(run, forward, right) * multiplier;
}

glm::vec3 FPSController::updateHorizontalMovement(float dt, const FPSInput& input) {
    const float yaw = glm::radians(input.yawDegrees);
    const glm::vec2 forward(std::cos(yaw), std::sin(yaw));
    const glm::vec2 right(-forward.y, forward.x);
    glm::vec2 target = forward * input.forward + right * input.right;
    const float length = glm::length(target);
    if (length > 1) target /= length;
    // Charge intent changes once, weighted by current speed. Holding a target
    // during acceleration must not charge the same velocity error every tick.
    const float intentLength = glm::length(target);
    const glm::vec2 intent = intentLength > 0 ? target / intentLength : glm::vec2(0);
    const float speed = glm::length(horizontalVelocity_);
    if (intentLength > 0 && speed > 0) {
        const float turn = std::min(glm::length(intent - movementIntent_),
            glm::length(intent - horizontalVelocity_ / speed));
        exertion_.load += config_.exertion.directionLoad * turn * std::min(1.f, speed / config_.runSpeed);
    }
    movementIntent_ = intent;
    target *= movementSpeed(input.run, input.forward, input.right);

    const glm::vec2 delta = target - horizontalVelocity_;
    const float distance = glm::length(delta);
    if (distance > 0) {
        const auto direction = delta / distance;
        // One shared acceleration in m/s²: forward walk -> run takes 2.5 s.
        const float acceleration = (config_.runSpeed - config_.walkSpeed) / 2.5f * exertion_.accelerationMultiplier();
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
