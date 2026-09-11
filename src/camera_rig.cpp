#include "camera_rig.hpp"

#include <iostream>

namespace viewer {

CameraRig::CameraRig(const CollisionWorld& world, glm::vec3 minimum,
                     glm::vec3 maximum, float aspect)
    : world_(world), minimum_(minimum), maximum_(maximum) {
    camera_.frame(minimum, maximum, aspect);
}

bool CameraRig::enterWalking(std::optional<glm::vec3> preferredEye) {
    if (walking() && !preferredEye) return true;
    // Construct a candidate first: failed placement cannot disturb a running
    // controller or change the current camera mode.
    FPSController candidate(player_.config());
    if (!candidate.spawn(preferredEye.value_or(camera_.position()), world_)) {
        std::cerr << "Cannot enter FPS mode: no clear, walkable spawn found. "
                  << "Fly near open ground or use --spawn X Z.\n";
        return false;
    }
    player_ = candidate;
    hasPlayer_ = true;
    camera_.setPosition(player_.eyePosition());
    mode_ = CameraMode::Walking;
    const auto feet = player_.feet();
    std::cout << "Camera mode: FPS walking; feet (" << feet.x << ", " << feet.y << ", " << feet.z
              << "); eye height " << player_.config().eyeHeight << " m\n";
    return true;
}

void CameraRig::enterFreeFly() {
    if (mode_ != CameraMode::FreeFly) {
        mode_ = CameraMode::FreeFly;
        std::cout << "Camera mode: Free-fly\n";
    }
}

void CameraRig::frame(float aspect) {
    if (!walking()) camera_.frame(minimum_, maximum_, aspect);
}

void CameraRig::update(float dt, float forward, float right, float up, bool run, bool jump) {
    if (walking()) {
        player_.update(dt, {forward, right, camera_.yawDegrees(), run, jump}, world_);
        camera_.setPosition(player_.eyePosition());
    } else camera_.move(forward, right, up, dt, run);
}

float CameraRig::speed(bool run) const {
    if (!walking()) return camera_.speed();
    return run ? player_.config().runSpeed : player_.config().walkSpeed;
}

} // namespace viewer
