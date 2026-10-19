#include "weapon_motion.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
struct Profile { float rearward; glm::vec3 angles; double recovery; };
// Pistol: sharp wrist flip. SMG: small, fast pulses. Shotgun: heavy shoulder
// kick followed by a pump. Sniper: firm kick with the longest recovery.
constexpr std::array<Profile, 4> profiles{{
    {.035f, {6.0f, -.7f, -1.2f}, .26},
    {.018f, {2.2f, .25f, .4f}, .14},
    {.075f, {9.0f, -.8f, -2.0f}, .55},
    {.055f, {6.5f, .45f, 1.0f}, .70}
}};

float remaining(double elapsed, double duration) {
    if (!std::isfinite(elapsed) || elapsed < 0 || elapsed >= duration) return 0;
    const float t = static_cast<float>(1 - elapsed / duration);
    return t*t; // Immediate kick, smooth recovery to an exact rest pose.
}

float smooth(double t) {
    const auto x = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return x*x*(3-2*x);
}
} // namespace

void WeaponMotion::fire(std::size_t slot, double seconds) {
    if (slot >= shots_.size() || !std::isfinite(seconds)) return;
    auto& shot = shots_[slot];
    if (seconds < shot.seconds) return;
    const float residual = shot.strength * remaining(seconds-shot.seconds, profiles[slot].recovery);
    shot.strength = std::min(1.6f, 1 + .35f*residual);
    shot.seconds = seconds;
}

WeaponMotionPose WeaponMotion::pose(std::size_t slot, double seconds) const {
    WeaponMotionPose result;
    if (slot >= shots_.size() || !std::isfinite(seconds)) return result;
    const auto& shot = shots_[slot];
    const auto& profile = profiles[slot];
    const double elapsed = seconds-shot.seconds;
    const float kick = shot.strength * remaining(elapsed, profile.recovery);
    result.offset.z = profile.rearward*kick;
    result.angles = profile.angles*kick;
    // Pull the separate fore-end back 9.5 cm, then return it before the
    // default 0.9 s firing cooldown. Absolute timing survives frame stalls.
    if (slot == 2 && elapsed > .12 && elapsed < .66)
        result.pumpTravel = .095f * (elapsed < .36 ? smooth((elapsed-.12)/.24)
                                                 : 1-smooth((elapsed-.36)/.30));
    return result;
}

Camera WeaponMotion::aimCamera(const Camera& input, std::size_t slot, double seconds, bool combat) const {
    auto result = input;
    if (!combat || slot != 3 || input.viewMode() == Camera::ViewMode::original || !std::isfinite(seconds))
        return result;
    // Scale angular amplitude with FOV for comparable screen motion at both
    // zooms (up to roughly 20 pixels at 720p). The reticle stays on the aim ray.
    const float scale = input.viewMode() == Camera::ViewMode::zoom1
        ? std::tan(glm::radians(.5f)) / std::tan(glm::radians(5.0f)) : 1;
    const float yaw = static_cast<float>(.210*std::sin(seconds*1.37) + .060*std::sin(seconds*2.43));
    const float pitch = static_cast<float>(.168*std::sin(seconds*2.1));
    const auto kick = pose(slot, seconds);
    result.setOrientation(input.yawDegrees() + scale*(yaw + kick.angles.y*.04f),
                          input.pitchDegrees() + scale*(pitch + kick.angles.x*.045f));
    return result;
}

glm::mat4 animatedWeaponPose(const glm::mat4& rest, const WeaponMotionPose& motion) {
    auto result = glm::translate(glm::mat4(1), motion.offset) * rest;
    result = glm::rotate(result, glm::radians(motion.angles.x), {1,0,0});
    result = glm::rotate(result, glm::radians(motion.angles.y), {0,1,0});
    return glm::rotate(result, glm::radians(motion.angles.z), {0,0,1});
}

} // namespace viewer
