#include "fps_controller.hpp"
#include "collision_world.hpp"

#include <algorithm>

namespace viewer {
float FPSController::stanceHeight(PlayerStance stance) const {
    const float requested = stance == PlayerStance::Standing ? config_.height :
        stance == PlayerStance::Crouched ? config_.crouchHeight : config_.proneHeight;
    // Preserve valid custom standing capsules, including unusual radii/eye
    // offsets. Every stance must still fit two rounded caps and the eye inset.
    const float minimum = std::max(2 * config_.radius,
        config_.height - config_.eyeHeight + std::min(.05f, config_.eyeHeight));
    return std::clamp(requested, minimum, config_.height);
}
float FPSController::bodyHeight() const { return stanceHeight(stance_); }
float FPSController::eyeHeight() const { return bodyHeight() - (config_.height - config_.eyeHeight); }
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
    if (nextHeight > previousHeight) {
        for (const auto& contact : world.contacts(candidate, config_.radius, nextHeight))
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
