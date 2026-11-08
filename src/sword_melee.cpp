#include "sword_melee.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace viewer {
void SwordMelee::trigger(bool held) {
    if (held && !held_) { armed_ = true; drag_ = {}; }
    if (!held) { armed_ = false; drag_ = {}; }
    held_ = held;
}
void SwordMelee::drag(glm::vec2 pixels) {
    if (held_ && armed_ && std::isfinite(pixels.x) && std::isfinite(pixels.y))
        drag_ = glm::clamp(drag_ + pixels, glm::vec2(-2000), glm::vec2(2000));
}
bool SwordMelee::update(double seconds) {
    if (!std::isfinite(seconds) || !held_ || !armed_) return false;
    if (seconds < startedAt_ + swordDurationSeconds) {
        drag_ = {}; // Recovery motion never queues or contributes to the next cut.
        return false;
    }
    if (glm::length(drag_) < 12) return false;
    direction_ = glm::normalize(drag_);
    drag_ = {}; // Another cut needs fresh movement, even while the button stays held.
    startedAt_ = seconds;
    stoppedElapsed_ = -1;
    return true;
}
void SwordMelee::cancel() {
    armed_ = false; drag_ = {}; startedAt_ = -std::numeric_limits<double>::infinity();
    stoppedElapsed_ = -1;
    // Keep the physical hold latched until a real release.
}
void SwordMelee::stopAt(float elapsed) {
    if (std::isfinite(elapsed) && elapsed >= swordWindupSeconds &&
        elapsed <= swordWindupSeconds + swordCutSeconds &&
        (stoppedElapsed_ < 0 || elapsed < stoppedElapsed_)) stoppedElapsed_ = elapsed;
}
SwordPose SwordMelee::pose(double seconds) const {
    const double elapsed = seconds - startedAt_;
    return {direction_, std::isfinite(elapsed) && elapsed >= 0 && elapsed < swordDurationSeconds ?
        static_cast<float>(elapsed) : -1.f, stoppedElapsed_};
}
glm::mat4 swordRestPose() {
    return glm::translate(glm::mat4(1), {.30f, -.20f, -.95f}) *
        glm::rotate(glm::mat4(1), glm::radians(-25.f), glm::vec3(1, 0, 0));
}
WeaponMotionPose swordMotion(const SwordPose& pose) {
    WeaponMotionPose motion;
    if (pose.elapsed < 0) return motion;
    const float swingElapsed = pose.stoppedElapsed < 0 ? pose.elapsed : std::min(pose.elapsed, pose.stoppedElapsed);
    const float cut = std::clamp((swingElapsed - swordWindupSeconds) / swordCutSeconds, 0.f, 1.f);
    const float angle = glm::radians(65.f - 130.f * cut);
    const auto axis = glm::vec3(-pose.direction.y, pose.direction.x, 0);
    const auto swing = glm::translate(glm::mat4(1), {0.f, -.06f, -.72f}) *
        glm::rotate(glm::mat4(1), angle, axis) *
        glm::rotate(glm::mat4(1), std::atan2(pose.direction.y, pose.direction.x), glm::vec3(0, 0, 1)) *
        glm::rotate(glm::mat4(1), glm::radians(-90.f), glm::vec3(1, 0, 0));
    float recovery = std::clamp((pose.elapsed - swordWindupSeconds - swordCutSeconds) /
        (swordDurationSeconds - swordWindupSeconds - swordCutSeconds), 0.f, 1.f);
    recovery = recovery * recovery * (3 - 2 * recovery);
    // Blend rigid rotations and positions back to guard after the damaging cut.
    const auto rest = swordRestPose();
    const auto rotation = glm::mat4_cast(glm::slerp(glm::quat_cast(swing), glm::quat_cast(rest), recovery));
    auto transform = rotation;
    transform[3] = glm::mix(swing[3], rest[3], recovery);
    motion.localTransform = glm::inverse(rest) * transform;
    return motion;
}
glm::mat4 swordTransform(const SwordPose& pose) {
    return animatedWeaponPose(swordRestPose(), swordMotion(pose));
}
SwordPose advanceMeleePose(SwordPose pose, double secondsSinceSample) {
    if (pose.elapsed >= 0 && std::isfinite(secondsSinceSample))
        pose.elapsed += static_cast<float>(std::max(0.0, secondsSinceSample));
    if (pose.elapsed >= swordDurationSeconds) pose.elapsed = -1;
    return pose;
}
} // namespace viewer
