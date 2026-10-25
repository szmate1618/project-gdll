#include "player_presentation.hpp"

#include <algorithm>

namespace viewer {
void PlayerPresentation::reset(glm::vec3 eye, const PlayerBodyPose& pose) {
    previousEye_ = currentEye_ = eye;
    previousPose_ = currentPose_ = pose;
    initialized_ = true;
}
void PlayerPresentation::advance(glm::vec3 eye, const PlayerBodyPose& pose) {
    if (!initialized_ || glm::distance(currentEye_, eye) > 2.f) {
        reset(eye, pose);
        return;
    }
    previousEye_ = currentEye_;
    previousPose_ = currentPose_;
    currentEye_ = eye;
    currentPose_ = pose;
}
glm::vec3 PlayerPresentation::eye(float alpha) const {
    return glm::mix(previousEye_, currentEye_, std::clamp(alpha, 0.f, 1.f));
}
PlayerBodyPose PlayerPresentation::pose(float alpha) const {
    auto result = interpolatePlayerBodyPose(previousPose_, currentPose_, std::clamp(alpha, 0.f, 1.f));
    if (result.enabled) {
        // Enabling uses the complete new anatomy rather than an absent prior
        // pose. Carry that anatomy with the same interpolated camera position.
        const auto offset = eye(alpha) - result.eye;
        for (auto& part : result.parts) part[3] += glm::vec4(offset, 0);
        result.eye += offset;
    }
    return result;
}
} // namespace viewer
