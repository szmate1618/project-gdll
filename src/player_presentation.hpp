#pragma once

#include "player_body_pose.hpp"

namespace viewer {
// Render the camera and physical body between the same two solved steps.
// Gameplay queries continue to use the current simulation pose.
class PlayerPresentation {
public:
    void reset(glm::vec3 eye, const PlayerBodyPose& pose);
    void advance(glm::vec3 eye, const PlayerBodyPose& pose);
    glm::vec3 eye(float alpha) const;
    PlayerBodyPose pose(float alpha) const;
    glm::vec3 currentEye() const { return currentEye_; }
private:
    glm::vec3 previousEye_{0}, currentEye_{0};
    PlayerBodyPose previousPose_{}, currentPose_{};
    bool initialized_ = false;
};
} // namespace viewer
