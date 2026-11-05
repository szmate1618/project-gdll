#include "fps_controller.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
float FPSController::stanceHeight(PlayerStance stance) const {
    if (stance == PlayerStance::Prone) return std::min(config_.proneHeight, config_.height);
    const float requested = stance == PlayerStance::Standing ? config_.height :
        stance == PlayerStance::Crouched ? config_.crouchHeight : config_.proneHeight;
    // Preserve valid custom standing capsules, including unusual radii/eye
    // offsets. Every stance must still fit two rounded caps and the eye inset.
    const float minimum = std::max(2 * config_.radius,
        config_.height - config_.eyeHeight + std::min(.05f, config_.eyeHeight));
    return std::clamp(requested, minimum, config_.height);
}
float FPSController::postureHeight(float posture) const {
    const float crouch = stanceHeight(PlayerStance::Crouched);
    return posture <= 1 ? glm::mix(config_.height, crouch, posture) :
        glm::mix(crouch, stanceHeight(PlayerStance::Prone), posture - 1);
}
glm::vec2 FPSController::groundSlope() const {
    const float squared = glm::dot(groundNormal_, groundNormal_);
    if (!std::isfinite(squared) || squared <= .000001f || !walkable(groundNormal_ / std::sqrt(squared))) return {0, 0};
    return {-groundNormal_.x / groundNormal_.y, -groundNormal_.z / groundNormal_.y};
}
float FPSController::postureEyeHeight(float posture, float yawDegrees) const {
    const float crouch = stanceHeight(PlayerStance::Crouched) - (config_.height - config_.eyeHeight);
    if (posture <= 1) return glm::mix(config_.eyeHeight, crouch, posture);
    const float blend = posture - 1;
    const float nominal = glm::mix(crouch, std::min(config_.proneEyeHeight,
        stanceHeight(PlayerStance::Prone)), blend);
    const auto slope = groundSlope();
    const float inverseNormalY = std::sqrt(1 + glm::dot(slope, slope));
    const auto box = proneBox(feet_, yawDegrees, posture);
    const auto axes = glm::mat3_cast(box.rotation);
    const glm::vec2 offset(box.center.x - feet_.x, box.center.z - feet_.z);
    const float boxRise = glm::dot(slope, offset) +
        std::abs(glm::dot(slope, glm::vec2(axes[0].x, axes[0].z))) * (box.halfExtents.x + config_.skin) +
        std::abs(glm::dot(slope, glm::vec2(axes[2].x, axes[2].z))) * (box.halfExtents.z + config_.skin) + config_.skin;
    const float capsuleRise = config_.radius * (inverseNormalY - 1) + config_.skin * inverseNormalY;
    // Flat box corners rest higher than rounded capsule feet on a slope.
    // Cancel that base change at the shape boundary, fading the correction out
    // toward the authored prone eye height. No extra reconciliation state.
    return std::clamp(nominal - (boxRise - capsuleRise) * (1 - blend),
        std::min(.05f, nominal), postureHeight(posture));
}
float FPSController::bodyHeight() const { return postureHeight(posture_); }
float FPSController::eyeHeight() const { return postureEyeHeight(posture_, bodyYawDegrees_); }
float FPSController::bodyRadius() const {
    return posture_ > 1 ? std::min(config_.proneWidth * .5f, bodyHeight() * .5f) : config_.radius;
}
bool FPSController::changePosture(float next, const CollisionWorld& world) {
    if (next == posture_) return true;
    auto candidate = feet_;
    // In air a gradual leg tuck must retain the same ballistic eye trajectory.
    if (!grounded_) candidate.y += eyeHeight() - postureEyeHeight(next, bodyYawDegrees_);
    if (next > 1 && grounded_) {
        if (const auto support = proneSupport(candidate, next, world, candidate.y - config_.radius,
                                              candidate.y + config_.stepHeight + config_.skin))
            candidate.y = std::max(candidate.y, support->first + config_.skin);
    }
    // Shrinking an upright capsule stays within its former volume. Every
    // expansion and box morph checks the actual intermediate shape instead.
    if (next < posture_ || next > 1 || posture_ > 1) {
        for (const auto& contact : contactsAt(candidate, next, world, config_.skin))
            if (contact.depth > .0001f) return false;
    }
    feet_ = candidate;
    posture_ = next;
    return true;
}

void FPSController::updateStance(float dt, bool crouch, const CollisionWorld& world) {
    // Prone is queued until landing, unless already prone when walking off an
    // edge. Crouch still permits a leg tuck during that airborne interval.
    const auto desired = proneRequested_ && (grounded_ || stance_ == PlayerStance::Prone) ? PlayerStance::Prone :
        crouch ? PlayerStance::Crouched : PlayerStance::Standing;
    const float target = static_cast<float>(desired);
    float remaining = dt;
    // Split a step at crouch so adjacent durations stay consistent in either
    // direction and when a request reverses halfway through a transition.
    for (int segment = 0; segment < 2 && remaining > 0 && posture_ != target; ++segment) {
        const bool lowering = target > posture_;
        const bool proneSegment = lowering ? posture_ >= 1 : posture_ > 1;
        const float boundary = lowering ? std::min(target, proneSegment ? 2.f : 1.f) :
            std::max(target, proneSegment ? 1.f : 0.f);
        const float seconds = proneSegment ? config_.proneTransitionSeconds : config_.crouchTransitionSeconds;
        const float amount = std::min(std::abs(boundary - posture_), remaining / seconds);
        const float next = amount >= std::abs(boundary - posture_) ? boundary :
            posture_ + (lowering ? amount : -amount);
        if (!changePosture(next, world)) {
            // Approach the last clear pose without crossing thin ceilings or
            // newly expanded torso corners. Retry the requested pose next tick.
            float blocked = next;
            for (int i = 0; i < 8; ++i) {
                const float middle = (posture_ + blocked) * .5f;
                if (!changePosture(middle, world)) blocked = middle;
            }
            break;
        }
        remaining = std::max(0.f, remaining - amount * seconds);
    }
    // Use the slower stance throughout a transition, including getting up.
    // Collision and sight targets always use the continuous physical posture.
    const float active = std::max(posture_, target);
    stance_ = active > 1 ? PlayerStance::Prone : active > 0 ? PlayerStance::Crouched : PlayerStance::Standing;
}
} // namespace viewer
