#include "fps_controller.hpp"
#include "collision_world.hpp"

#include <algorithm>

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
float FPSController::bodyHeight() const { return stanceHeight(stance_); }
float FPSController::eyeHeight() const {
    return stance_ == PlayerStance::Prone ? std::min(config_.proneEyeHeight, bodyHeight()) :
        bodyHeight() - (config_.height - config_.eyeHeight);
}
float FPSController::bodyRadius() const {
    return stance_ == PlayerStance::Prone ? std::min(config_.proneWidth * .5f, bodyHeight() * .5f) : config_.radius;
}
float FPSController::movementSpeed(bool run) const {
    const auto movingStance = grounded_ ? stance_ : takeoffStance_;
    if (movingStance == PlayerStance::Prone) return config_.proneSpeed;
    if (movingStance == PlayerStance::Crouched) return config_.crouchSpeed;
    return run ? config_.runSpeed : config_.walkSpeed;
}

bool FPSController::changeStance(PlayerStance next, const CollisionWorld& world) {
    if (next == stance_) return true;
    const float previousHeight = bodyHeight(), nextHeight = stanceHeight(next);
    auto candidate = feet_;
    // Grounded crouch lowers the head around planted feet. In the air, keep
    // the head/camera on its existing ballistic path and move only the feet.
    if (!grounded_) candidate.y += previousHeight - nextHeight;
    if (next == PlayerStance::Prone && grounded_) {
        if (const auto support = proneSupport(candidate, world, candidate.y - config_.radius,
                                              candidate.y + config_.stepHeight + config_.skin))
            candidate.y = std::max(candidate.y, support->first + config_.skin);
    }
    // Prone expands the footprint even while lowering the head. Every shape
    // change needs clearance, including the torso behind the camera.
    if (nextHeight > previousHeight || next == PlayerStance::Prone || stance_ == PlayerStance::Prone) {
        for (const auto& contact : contactsAt(candidate, next, world))
            if (contact.depth > config_.skin + .0001f) return false;
    }
    feet_ = candidate;
    stance_ = next;
    return true;
}

void FPSController::updateStance(bool crouch, const CollisionWorld& world) {
    // Prone is queued until landing, unless already prone when walking off an
    // edge. Crouch still permits a leg tuck during that airborne interval.
    const auto desired = proneRequested_ && (grounded_ || stance_ == PlayerStance::Prone) ? PlayerStance::Prone :
        crouch ? PlayerStance::Crouched : PlayerStance::Standing;
    if (!changeStance(desired, world) && desired == PlayerStance::Standing && stance_ == PlayerStance::Prone)
        changeStance(PlayerStance::Crouched, world);
}
} // namespace viewer
