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
    const float stamina = run && movingStance == PlayerStance::Standing ? exertion_.speedMultiplier() : 1;
    const float multiplier = std::min(stamina, injuryMovement_.speed);
    return baseMovementSpeed(run, forward, right) * multiplier;
}

glm::vec3 FPSController::updateHorizontalMovement(float dt, const FPSInput& input) {
    const float yaw = glm::radians(input.yawDegrees);
    const glm::vec2 forward(std::cos(yaw), std::sin(yaw));
    const glm::vec2 right(-forward.y, forward.x);
    glm::vec2 target(input.forward, input.right);
    const float length = glm::length(target);
    if (length > 1) target /= length;
    // Charge intent changes once, weighted by current speed. Holding a target
    // during acceleration must not charge the same velocity error every tick.
    const float intentLength = glm::length(target);
    const glm::vec2 intent = intentLength > 0 ? (forward * target.x + right * target.y) / intentLength : glm::vec2(0);
    const float speed = glm::length(horizontalVelocity_);
    if (intentLength > 0 && speed > 0) {
        const float turn = std::min(glm::length(intent - movementIntent_),
            glm::length(intent - horizontalVelocity_ / speed));
        exertion_.load += config_.exertion.directionLoad * turn * std::min(1.f, speed / config_.runSpeed);
    }
    movementIntent_ = intent;
    target *= movementSpeed(input.run, input.forward, input.right);

    // Project world momentum onto the current input axes; looking around must
    // not rotate the existing velocity with the camera.
    const glm::vec2 velocity(glm::dot(horizontalVelocity_, forward), glm::dot(horizontalVelocity_, right));
    const glm::vec2 delta = target - velocity;
    // One shared acceleration in m/s²: forward walk -> run takes 0.3125 s.
    const float baseAcceleration = (config_.runSpeed - config_.walkSpeed) / .3125f;
    // Lateral force uses the opposite leg, including when braking or reversing.
    const glm::vec2 injuryAcceleration(injuryMovement_.forwardAcceleration,
        delta.y < 0 ? injuryMovement_.leftAcceleration : injuryMovement_.rightAcceleration);
    const glm::vec2 acceleration = baseAcceleration * glm::min(injuryAcceleration,
        glm::vec2(exertion_.accelerationMultiplier()));
    glm::vec2 brakingTime(0), effort(0);
    for (int axis = 0; axis < 2; ++axis) {
        const float distance = std::abs(delta[axis]);
        const float brakingDistance = velocity[axis] * delta[axis] < 0 ?
            std::min(std::abs(velocity[axis]), distance) : 0;
        brakingTime[axis] = brakingDistance / (2 * acceleration[axis]);
        // Effort is seconds at full axis strength, with reversal split at zero.
        effort[axis] = std::min(dt, brakingTime[axis] + (distance - brakingDistance) / acceleration[axis]);
    }
    const float totalEffort = glm::length(effort);
    if (totalEffort > dt) effort *= dt / totalEffort;
    glm::vec2 change(0);
    for (int axis = 0; axis < 2; ++axis) {
        // Braking consumes its share at 2a, then any remaining share uses a.
        const float travel = acceleration[axis] * (effort[axis] + std::min(effort[axis], brakingTime[axis]));
        change[axis] = std::copysign(std::min(travel, std::abs(delta[axis])), delta[axis]);
    }
    if (change == delta) horizontalVelocity_ = forward * target.x + right * target.y;
    else horizontalVelocity_ += forward * change.x + right * change.y;
    return {horizontalVelocity_.x, 0, horizontalVelocity_.y};
}

} // namespace viewer
