#include "zombie_update_profile.hpp"
#include "zombie_chase.hpp"
#include "zombie_layer.hpp"
#include "zombie_movement.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr double tau = 6.283185307179586;
constexpr float startAnimationSpeed = .20f, stopAnimationSpeed = .10f; // m/s.
constexpr float animationHoldSeconds = .15f;
constexpr float locomotionTurnSpeed = static_cast<float>(tau / 2); // 180 degrees/s.
}

void stopZombieLocomotion(ZombieInstance& instance) {
    instance.walking = false;
    instance.locomotionVelocity = glm::vec3(0);
    instance.blockedAnimationSeconds = instance.animationRestartSeconds = 0;
}

bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds) {
    const float yaw = std::atan2(instance.restRoot[2].x, instance.restRoot[2].z);
    const float targetYaw = std::atan2(direction.x, direction.y);
    const float delta = std::remainder(targetYaw - yaw, static_cast<float>(tau));
    const float maxTurn = static_cast<float>(tau / 4) * seconds;
    const bool aligned = std::abs(delta) <= maxTurn + 1e-5f;
    const float nextYaw = aligned ? targetYaw : yaw + std::clamp(delta, -maxTurn, maxTurn);
    const auto feet = glm::vec3(instance.restRoot[3]);
    instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), nextYaw, {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    stopZombieLocomotion(instance);
    return aligned;
}

void applyZombieLocomotion(ZombieInstance& instance, glm::vec2 desiredVelocity,
                           const ZombieMovement& movement, float speed, float seconds) {
    zombie_profile::Timer timer(zombie_profile::Stage::locomotionPose);
    const auto start = glm::vec3(instance.restRoot[3]);
    const auto feet = movement.feet;
    instance.locomotionVelocity = seconds > 0 ? (feet - start) / seconds : glm::vec3(0);
    const float magnitude = glm::length(desiredVelocity);
    const auto direction = magnitude > 1e-8f ? desiredVelocity / magnitude : glm::vec2(0);
    const float step = magnitude * seconds;
    const auto travelled = glm::vec2(feet.x - start.x, feet.z - start.z);
    const float travel = glm::length(travelled);
    const float threshold = instance.walking ? stopAnimationSpeed : startAnimationSpeed;
    const bool locomotion = travel >= threshold * seconds && glm::dot(travelled, direction) > 0;
    instance.animationRestartSeconds = std::max(0.f, instance.animationRestartSeconds - seconds);
    if (locomotion) {
        instance.blockedAnimationSeconds = 0;
        if (instance.animationRestartSeconds == 0) instance.walking = true;
    } else if (instance.walking) {
        instance.blockedAnimationSeconds += seconds;
        if (instance.blockedAnimationSeconds >= animationHoldSeconds) {
            instance.walking = false;
            instance.animationRestartSeconds = animationHoldSeconds;
        }
    }
    const bool smallAdjustment = travel < .02f && travel < step * .5f;
    if (feet != start || (locomotion && !smallAdjustment)) {
        const float yaw = std::atan2(instance.restRoot[2].x, instance.restRoot[2].z);
        float nextYaw = yaw;
        // Tiny contact slides may go backward or sideways. Keep their physical
        // clearance correction without reorienting the whole animated body.
        if (locomotion && !smallAdjustment) {
            const float targetYaw = std::atan2(travelled.x, travelled.y);
            const float delta = std::remainder(targetYaw - yaw, static_cast<float>(tau));
            const float turn = locomotionTurnSpeed * seconds;
            nextYaw += std::clamp(delta, -turn, turn);
        }
        if (feet != start || nextYaw != yaw) {
            instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), nextYaw, {0, 1, 0});
            instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
        }
    }
    instance.running = speed > 1.2f;
    // Advance the selected authored clip by actual travel time, excluding any
    // leftover step after arrival. No per-character skeleton is evaluated here.
    if (speed > 0 && instance.walking && locomotion) instance.walkSeconds += movement.travel / speed;
}

bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& /*asset*/,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double /*animationSeconds*/,
                      ZombieCrowd* crowd, std::size_t id) {
    const auto start = glm::vec3(instance.restRoot[3]);
    const glm::vec2 horizontal(target.x - start.x, target.z - start.z);
    const float distance = glm::length(horizontal);
    if (distance <= 1e-4f) {
        stopZombieLocomotion(instance);
        return true;
    }
    const auto direction = horizontal / distance;
    const float step = std::min(speed * seconds, distance);
    const auto movement = moveZombieBody(start,
        start + glm::vec3(direction.x * step, 0, direction.y * step), world, crowd, id);
    applyZombieLocomotion(instance, direction * (seconds > 0 ? step / seconds : 0.f), movement, speed, seconds);
    return glm::length(glm::vec2(target.x - movement.feet.x, target.z - movement.feet.z)) <= 1e-4f;
}

} // namespace viewer
