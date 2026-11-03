#include "object_interaction.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
constexpr float metersPerPixel = .004f;
constexpr float maxLiftForce = 180; // N: cannot suspend >18.4 kg.
constexpr float extraDragForce = 240; // N of additional joint budget for unliftable props.
}
std::optional<std::size_t> ObjectInteraction::heldBox() const {
    return grab_ ? grab_->boxes->indexOf(grab_->prop) : std::nullopt;
}
std::optional<PhysicsProp> ObjectInteraction::heldProp() const {
    return grab_ && grab_->prop.valid() && grab_->joint->valid() ? std::optional<PhysicsProp>(grab_->prop) : std::nullopt;
}
float ObjectInteraction::rayReach(glm::vec3 direction) {
    return std::min(1.5f/std::max(glm::length(glm::vec2(direction.x,direction.z)),1e-6f),
                    2.0f/std::max(std::abs(direction.y),1e-6f));
}
bool ObjectInteraction::press(const Camera& camera, BoxLayer& boxes, const CollisionWorld& world) {
    grab_.reset();
    const auto hit = raycastPhysicsProp(boxes.physicsWorld(),camera.position(),camera.direction(),rayReach(camera.direction()));
    if (!hit || !world.lineOfSight(camera.position(), hit->point)) return false;
    const float yaw = glm::radians(camera.yawDegrees());
    const glm::vec3 forward(std::cos(yaw), 0, std::sin(yaw));
    const auto right = glm::cross(forward, glm::vec3(0, 1, 0));
    // Keep the combined lift/drag budget below a heavy prop's weight.
    const float gripForce = std::max(maxLiftForce, std::min(maxLiftForce+extraDragForce,
                                                         hit->prop.mass()*9.8f*.9f));
    grab_ = Grab{&boxes, hit->prop, std::make_unique<PhysicsGrab>(hit->prop,hit->localPoint,gripForce),
                 hit->localPoint, hit->point - camera.position(), right, {}};
    return true;
}
bool ObjectInteraction::grip(const Camera& camera, BoxLayer& boxes, const PhysicsProp& prop) {
    if (!prop.valid() || prop.grabbed()) return false;
    const float yaw = glm::radians(camera.yawDegrees());
    const auto right = glm::vec3(-std::sin(yaw), 0, std::cos(yaw));
    grab_ = Grab{&boxes, prop, std::make_unique<PhysicsGrab>(prop, glm::vec3(0), maxLiftForce),
        glm::vec3(0), prop.center() - camera.position(), right, {}};
    return true;
}
void ObjectInteraction::cancel() { grab_.reset(); }
void ObjectInteraction::moveMouse(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !grab_) return;
    grab_->drag = glm::clamp(grab_->drag + glm::vec2(dx, dy) * metersPerPixel,
                            glm::vec2(-1.5f, -2), glm::vec2(1.5f, 2));
}
void ObjectInteraction::update(const Camera& camera, float dt) {
    if (!std::isfinite(dt) || dt <= 0 || !grab_) return;
    if (!heldProp()) { cancel(); return; }
    const auto point = grab_->prop.pointState(grab_->localPoint).point;
    const auto target = camera.position() + grab_->eyeOffset + grab_->right * grab_->drag.x + glm::vec3(0, grab_->drag.y, 0);
    if (glm::distance(point, camera.position()) > 5 || glm::distance(point, target) > 3) {
        cancel(); return;
    }
    grab_->joint->setTarget(target);
}
} // namespace viewer
