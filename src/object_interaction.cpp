#include "object_interaction.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
constexpr float metersPerPixel = .004f;
constexpr double swingWindow = .10; // Recent displacement per 100 ms, not event frequency.
constexpr float maxLiftForce = 180; // N per hand: one hand cannot suspend >18.4 kg.
constexpr float extraDragForce = 240; // N of additional joint budget for unliftable props.
constexpr float maxThrowImpulse = 60; // N*s per releasing hand.
bool finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
glm::vec3 bounded(glm::vec3 value, float maximum) {
    const float length = glm::length(value);
    return length > maximum ? value * (maximum / length) : value;
}
} // namespace
bool ObjectInteraction::grabbing() const { return heldProp(Hand::Left).has_value() || heldProp(Hand::Right).has_value(); }
std::optional<std::size_t> ObjectInteraction::heldBox(Hand hand) const {
    const auto& grab = grabs_[slot(hand)];
    return grab ? grab->boxes->indexOf(grab->prop) : std::nullopt;
}
std::optional<PhysicsProp> ObjectInteraction::heldProp(Hand hand) const {
    const auto& grab = grabs_[slot(hand)];
    return grab && grab->prop.valid() && grab->joint->valid() ? std::optional<PhysicsProp>(grab->prop) : std::nullopt;
}
float ObjectInteraction::rayReach(glm::vec3 direction) {
    return std::min(1.5f/std::max(glm::length(glm::vec2(direction.x,direction.z)),1e-6f),
                    2.0f/std::max(std::abs(direction.y),1e-6f));
}
bool ObjectInteraction::press(Hand hand, const Camera& camera, BoxLayer& boxes, const CollisionWorld& world, double seconds) {
    auto& grab = grabs_[slot(hand)];
    grab.reset();
    if (!std::isfinite(seconds)) return false;
    const auto hit = raycastPhysicsProp(boxes.physicsWorld(),camera.position(),camera.direction(),rayReach(camera.direction()));
    if (!hit || !world.lineOfSight(camera.position(), hit->point)) return false;
    const float yaw = glm::radians(camera.yawDegrees());
    const glm::vec3 forward(std::cos(yaw), 0, std::sin(yaw));
    const auto right = glm::cross(forward, glm::vec3(0, 1, 0));
    // The joint limits the total force vector. Give heavy props the combined
    // lift/drag budget, staying below their weight so one hand cannot lift them.
    const float gripForce = std::max(maxLiftForce, std::min(maxLiftForce+extraDragForce,
                                                         hit->prop.mass()*9.8f*.9f));
    grab = Grab{&boxes, hit->prop, std::make_unique<PhysicsGrab>(hit->prop,hit->localPoint,gripForce),
                hit->localPoint, hit->point - camera.position(), right, forward, {}, {}};
    return true;
}
glm::vec3 ObjectInteraction::throwVelocity(glm::vec2 swing, glm::vec3 right, glm::vec3 forward) {
    if (!finite(swing)) return glm::vec3(0);
    // Linear interpolation by the upward component: pure up adds 60% forward,
    // a horizontal or downward swing adds exactly zero forward momentum.
    return right * swing.x + glm::vec3(0, swing.y, 0) + forward * (std::max(0.0f, swing.y) * .6f);
}
void ObjectInteraction::release(Hand hand, double seconds) {
    auto& grab = grabs_[slot(hand)];
    if (!grab) return;
    if (!grab->prop.valid()) { grab.reset(); return; }
    const auto& other = grabs_[1 - slot(hand)];
    if (std::isfinite(seconds) && !(other && other->prop == grab->prop)) {
        glm::vec2 displacement(0);
        for (const auto& sample : grab->motion)
            if (sample.seconds <= seconds && seconds - sample.seconds < swingWindow) displacement += sample.displacement;
        auto velocity = throwVelocity(displacement / static_cast<float>(swingWindow), grab->right, grab->forward);
        velocity = bounded(velocity, 10.0f);
        const float mass = grab->prop.mass();
        // An object too heavy to suspend cannot become an upward projectile on
        // release. It can still receive a bounded horizontal shove along the floor.
        if (mass * 9.8f > maxLiftForce) velocity.y = std::min(0.0f, velocity.y);
        const auto impulse = bounded(velocity * mass, maxThrowImpulse);
        if (glm::length(impulse) > .001f)
            grab->prop.applyImpulse(impulse,grab->prop.center());
    }
    grab.reset();
}
void ObjectInteraction::cancel() { for (auto& grab : grabs_) grab.reset(); }
void ObjectInteraction::cancel(Hand hand) { grabs_[slot(hand)].reset(); }
void ObjectInteraction::moveMouse(float dx, float dy, double seconds) {
    const glm::vec2 motion(dx, dy);
    if (!finite(motion) || !std::isfinite(seconds)) return;
    for (auto& grab : grabs_) if (grab) {
        const auto displacement = motion * metersPerPixel;
        grab->drag = glm::clamp(grab->drag + displacement, glm::vec2(-1.5f, -2), glm::vec2(1.5f, 2));
        while (!grab->motion.empty() && seconds - grab->motion.front().seconds >= swingWindow) grab->motion.pop_front();
        grab->motion.push_back({seconds, displacement});
    }
}
void ObjectInteraction::update(const Camera& camera, float dt) {
    if (!std::isfinite(dt) || dt <= 0) return;
    for (auto& grab : grabs_) if (grab) {
        if (!grab->prop.valid() || !grab->joint->valid()) { grab.reset(); continue; }
        const auto point = grab->prop.pointState(grab->localPoint).point;
        const auto target = camera.position() + grab->eyeOffset + grab->right * grab->drag.x + glm::vec3(0, grab->drag.y, 0);
        if (glm::distance(point, camera.position()) > 5 || glm::distance(point, target) > 3) {
            grab.reset();
            continue;
        }
        grab->joint->setTarget(target);
    }
}
} // namespace viewer
