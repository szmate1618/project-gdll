#include "object_interaction.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
constexpr float metersPerPixel = .004f;
constexpr double swingWindow = .10; // Recent displacement per 100 ms, not event frequency.
constexpr float maxLiftForce = 180; // N per hand: one hand cannot suspend >18.4 kg.
constexpr float maxDragForce = 240; // N per hand, also available to grounded heavy props.
constexpr float maxThrowImpulse = 60; // N*s per releasing hand.
bool finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
glm::vec3 bounded(glm::vec3 value, float maximum) {
    const float length = glm::length(value);
    return length > maximum ? value * (maximum / length) : value;
}
} // namespace
bool ObjectInteraction::grabbing() const { return grabs_[0].has_value() || grabs_[1].has_value(); }
std::optional<std::size_t> ObjectInteraction::heldBox(Hand hand) const {
    const auto& grab = grabs_[slot(hand)];
    return grab ? std::optional<std::size_t>(grab->box) : std::nullopt;
}
bool ObjectInteraction::press(Hand hand, const Camera& camera, BoxLayer& boxes, const CollisionWorld& world, double seconds) {
    auto& grab = grabs_[slot(hand)];
    grab.reset();
    if (!std::isfinite(seconds)) return false;
    const auto hit = boxes.raycast(camera.position(), camera.direction(), 2.0f);
    if (!hit || !world.lineOfSight(camera.position(), hit->point)) return false;
    const float yaw = glm::radians(camera.yawDegrees());
    const glm::vec3 forward(std::cos(yaw), 0, std::sin(yaw));
    const auto right = glm::cross(forward, glm::vec3(0, 1, 0));
    grab = Grab{&boxes, hit->box, hit->localPoint, hit->point - camera.position(), right, forward, hit->point, {}, {}};
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
    const auto& other = grabs_[1 - slot(hand)];
    if (std::isfinite(seconds) && !(other && other->boxes == grab->boxes && other->box == grab->box)) {
        glm::vec2 displacement(0);
        for (const auto& sample : grab->motion)
            if (sample.seconds <= seconds && seconds - sample.seconds < swingWindow) displacement += sample.displacement;
        auto velocity = throwVelocity(displacement / static_cast<float>(swingWindow), grab->right, grab->forward);
        velocity = bounded(velocity, 10.0f);
        const float mass = grab->boxes->mass(grab->box);
        // An object too heavy to suspend cannot become an upward projectile on
        // release. It can still receive a bounded horizontal shove along the floor.
        if (mass * 9.8f > maxLiftForce) velocity.y = std::min(0.0f, velocity.y);
        const auto impulse = bounded(velocity * mass, maxThrowImpulse);
        if (glm::length(impulse) > .001f)
            grab->boxes->applyImpulse(grab->box, impulse, grab->boxes->pose(grab->box).center);
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
    const float step = std::min(dt, .1f);
    // Validate both grips before counting shared support or applying impulses.
    for (auto& grab : grabs_) if (grab) {
        const auto point = grab->boxes->pointState(grab->box, grab->localPoint).point;
        const auto target = camera.position() + grab->eyeOffset + grab->right * grab->drag.x + glm::vec3(0, grab->drag.y, 0);
        if (glm::distance(point, camera.position()) > 5 || glm::distance(point, target) > 3) grab.reset();
    }
    struct PendingImpulse { BoxLayer* boxes; std::size_t box; glm::vec3 impulse, point; };
    std::vector<PendingImpulse> impulses;
    for (auto& grab : grabs_) if (grab) {
        const auto state = grab->boxes->pointState(grab->box, grab->localPoint);
        const auto target = camera.position() + grab->eyeOffset + grab->right * grab->drag.x + glm::vec3(0, grab->drag.y, 0);
        const auto targetVelocity = bounded((target - grab->previousTarget) / step, 6.0f);
        grab->previousTarget = target;
        int hands = 0;
        for (const auto& active : grabs_)
            if (active && active->boxes == grab->boxes && active->box == grab->box) ++hands;
        const float mass = grab->boxes->mass(grab->box);
        constexpr float stiffness = 200; // N/m, per hand.
        const float damping = 2 * std::sqrt(stiffness * mass / hands);
        auto force = (target - state.point) * stiffness + (targetVelocity - state.velocity) * damping;
        // Split gravity compensation for shared objects; two grips must not
        // double-feed gravity and launch a light box when the mouse is still.
        force.y += mass * 9.8f / hands;
        force.y = glm::clamp(force.y, -maxLiftForce, maxLiftForce);
        const auto horizontal = bounded(glm::vec3(force.x, 0, force.z), maxDragForce);
        force.x = horizontal.x; force.z = horizontal.z;
        impulses.push_back({grab->boxes, grab->box, force * step, state.point});
    }
    // Both hands observe the same pre-impulse point velocity. Applying while
    // gathering would let the second hand damp the first hand's fresh impulse.
    for (const auto& impulse : impulses)
        impulse.boxes->applyImpulse(impulse.box, impulse.impulse, impulse.point);
}
} // namespace viewer
