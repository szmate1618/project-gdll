#include "door_interaction.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
void DoorInteraction::toggleMode() {
    release();
    mode_ = mode_ == ControlMode::Combat ? ControlMode::Interaction : ControlMode::Combat;
}
bool DoorInteraction::press(const Camera& camera, DoorLayer& doors, const CollisionWorld& world) {
    release();
    if (mode_ != ControlMode::Interaction) return false;
    const auto hit = doors.raycast(camera.position(), camera.direction(), 1.5f);
    if (!hit || !world.lineOfSight(camera.position(), hit->point)) return false;
    // Yaw defines a stable horizontal basis even when looking almost vertically.
    const float yaw = glm::radians(camera.yawDegrees());
    const glm::vec3 forward(std::cos(yaw), 0, std::sin(yaw));
    const auto right = glm::cross(forward, glm::vec3(0, 1, 0));
    grab_ = Grab{&doors, hit->door, hit->localPoint, hit->point - camera.position(), forward, right, glm::vec2(0)};
    return true;
}
void DoorInteraction::look(Camera& camera, float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    camera.look(dx, dy);
    if (!grab_) return;
    constexpr float metersPerPixel = .004f;
    grab_->drag += glm::vec2(dx, dy) * metersPerPixel;
    // Keep sustained dragging into a hinge stop bounded and easy to reverse.
    grab_->drag = glm::clamp(grab_->drag, glm::vec2(-1.5f), glm::vec2(1.5f));
}
void DoorInteraction::update(const Camera& camera, float dt) {
    if (!grab_ || !std::isfinite(dt) || dt <= 0) return;
    auto& grab = *grab_;
    const auto state = grab.doors->pointState(grab.door, grab.localPoint);
    const auto target = camera.position() + grab.eyeOffset + grab.right * grab.drag.x + grab.forward * grab.drag.y;
    // Spring stiffness in N/m and damping in N*s/m. Door hinges handle limits;
    // only horizontal forces reach the panel, including while jumping.
    auto force = (target - state.point) * 220.0f - state.velocity * 35.0f;
    force.y = 0;
    const float magnitude = glm::length(force);
    if (magnitude > 180.0f) force *= 180.0f / magnitude;
    // Box3D clears forces on each fixed step. An integrated impulse preserves
    // input across frames with zero or multiple physics steps.
    grab.doors->applyImpulse(grab.door, force * std::min(dt, .1f), state.point);
}
} // namespace viewer
