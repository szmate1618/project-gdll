#include "door_layer.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace viewer {
namespace {
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
b3Pos position(glm::vec3 p) { return {p.x, p.y, p.z}; }
b3Vec3 vector(glm::vec3 p) { return {p.x, p.y, p.z}; }
b3Quat rotation(glm::quat q) { return {{q.x, q.y, q.z}, q.w}; }
constexpr float cellSize = 32;
int cell(float value) {
    if (!std::isfinite(value) || std::abs(value) > 1e8f) throw std::invalid_argument("Door position outside supported local frame");
    return static_cast<int>(std::floor(value / cellSize));
}
} // namespace

struct DoorLayer::Impl {
    struct Entry {
        DoorPanel panel;
        b3BodyId anchor{}, body{};
        b3JointId joint{};
        glm::mat4 inverseRest{1};
    };
    std::shared_ptr<PhysicsWorld> world;
    std::vector<Entry> entries;
    std::vector<std::size_t> active;
    std::map<std::pair<int, int>, std::vector<std::size_t>> cells;
    float maximumReach = 0;
    glm::vec3 minimum{std::numeric_limits<float>::max()}, maximum{-minimum};

    ~Impl() {
        for (const auto index : active) {
            b3DestroyBody(entries[index].body); // Also destroys the attached joint.
            b3DestroyBody(entries[index].anchor);
        }
    }
    Entry& activate(std::size_t index) {
        auto& e = entries.at(index);
        if (b3Body_IsValid(e.body)) return e;
        auto definition = b3DefaultBodyDef();
        definition.position = position(e.panel.hinge);
        e.anchor = b3CreateBody(world->id(), &definition);
        definition.type = b3_dynamicBody;
        definition.position = position(e.panel.center);
        definition.rotation = rotation(e.panel.rotation);
        definition.angularDamping = 1.5f;
        definition.isAwake = false;
        e.body = b3CreateBody(world->id(), &definition);
        const auto half = e.panel.halfExtents;
        const auto box = b3MakeBoxHull(half.x, half.y, half.z);
        auto shape = b3DefaultShapeDef();
        shape.density = 25.0f / (8 * half.x * half.y * half.z); // 25 kg wooden panel.
        shape.baseMaterial.friction = .5f;
        b3CreateHullShape(e.body, &shape, &box.base);

        const glm::vec3 up(0, 1, 0);
        const auto right = glm::cross(up, e.panel.outward);
        // Revolute axis is local Z. Choosing -Y makes positive angles move the
        // free edge toward the building's outward normal, on every facade.
        const auto frame = rotation(glm::quat_cast(glm::mat3(right, e.panel.outward, -up)));
        auto hinge = b3DefaultRevoluteJointDef();
        hinge.base.bodyIdA = e.anchor;
        hinge.base.bodyIdB = e.body;
        hinge.base.localFrameA = b3Transform_identity;
        hinge.base.localFrameA.q = frame;
        hinge.base.localFrameB = b3Transform_identity;
        hinge.base.localFrameB.p = b3Body_GetLocalPoint(e.body, position(e.panel.hinge));
        hinge.base.localFrameB.q = b3InvMulQuat(definition.rotation, frame);
        hinge.base.collideConnected = false;
        hinge.enableLimit = true;
        hinge.lowerAngle = 0;
        hinge.upperAngle = 110 * B3_DEG_TO_RAD;
        e.joint = b3CreateRevoluteJoint(world->id(), &hinge);
        e.inverseRest = glm::inverse(glm::translate(glm::mat4(1), e.panel.center) * glm::mat4_cast(e.panel.rotation));
        active.push_back(index);
        return e;
    }
};

DoorLayer::DoorLayer(std::vector<DoorPanel> panels, std::shared_ptr<PhysicsWorld> world)
    : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Doors require a physics world");
    impl_->world = std::move(world);
    impl_->active.reserve(panels.size());
    for (const auto& p : panels) {
        const auto h = p.halfExtents;
        if (!finite(p.center) || !finite(p.hinge) || !finite(h) || h.x <= 0 || h.y <= 0 || h.z <= 0 ||
            !finite(p.outward) || std::abs(glm::length(p.outward) - 1) > .001f || std::abs(p.outward.y) > .001f ||
            !std::isfinite(glm::length(p.rotation)) || std::abs(glm::length(p.rotation) - 1) > .001f)
            throw std::invalid_argument("Invalid door geometry");
        const float density = 25.0f / (8 * h.x * h.y * h.z);
        if (!std::isfinite(density) || density <= 0) throw std::invalid_argument("Invalid door collision volume");
        for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
            if (!std::isfinite(p.closedTransform[column][row])) throw std::invalid_argument("Door transform must be finite");
        const auto key = std::make_pair(cell(p.hinge.x), cell(p.hinge.z));
        impl_->cells[key].push_back(impl_->entries.size());
        impl_->entries.push_back({p, {}, {}, {}, glm::mat4(1)});
        impl_->maximumReach = std::max(impl_->maximumReach, 2 * h.x + h.z + .02f);
        const glm::vec3 swept(2 * h.x + h.z + .02f, h.y + .02f, 2 * h.x + h.z + .02f);
        impl_->minimum = glm::min(impl_->minimum, p.hinge - swept);
        impl_->maximum = glm::max(impl_->maximum, p.hinge + swept);
    }
}
DoorLayer::~DoorLayer() = default;
std::size_t DoorLayer::count() const { return impl_->entries.size(); }
std::size_t DoorLayer::activeCount() const { return impl_->active.size(); }
const DoorPanel& DoorLayer::panel(std::size_t door) const { return impl_->entries.at(door).panel; }

void DoorLayer::activateNear(glm::vec3 p, float radius) {
    if (!finite(p) || !std::isfinite(radius) || radius < 0 || radius > 1024)
        throw std::invalid_argument("Invalid door activation region");
    const int left = cell(p.x - radius), right = cell(p.x + radius);
    const int first = cell(p.z - radius), last = cell(p.z + radius);
    for (int x = left; x <= right; ++x) for (int z = first; z <= last; ++z) {
        const auto found = impl_->cells.find({x, z});
        if (found == impl_->cells.end()) continue;
        for (const auto index : found->second) {
            const auto delta = impl_->entries[index].panel.hinge - p;
            if (glm::dot(delta, delta) <= radius * radius) impl_->activate(index);
        }
    }
}
void DoorLayer::applyForce(std::size_t door, glm::vec3 force, glm::vec3 point) {
    if (!finite(force) || !finite(point)) throw std::invalid_argument("Door force must be finite");
    b3Body_ApplyForce(impl_->activate(door).body, vector(force), position(point), true);
}
void DoorLayer::applyImpulse(std::size_t door, glm::vec3 impulse, glm::vec3 point) {
    if (!finite(impulse) || !finite(point)) throw std::invalid_argument("Door impulse must be finite");
    b3Body_ApplyLinearImpulse(impl_->activate(door).body, vector(impulse), position(point), true);
}
float DoorLayer::angle(std::size_t door) const {
    const auto& e = impl_->entries.at(door);
    return b3Body_IsValid(e.body) ? b3RevoluteJoint_GetAngle(e.joint) : 0;
}
glm::mat4 DoorLayer::transform(std::size_t door) const {
    const auto& e = impl_->entries.at(door);
    if (!b3Body_IsValid(e.body)) return e.panel.closedTransform;
    const auto p = b3Body_GetPosition(e.body);
    const auto q = b3Body_GetRotation(e.body);
    const auto current = glm::translate(glm::mat4(1), glm::vec3(p.x, p.y, p.z)) *
        glm::mat4_cast(glm::quat(q.s, q.v.x, q.v.y, q.v.z));
    return current * e.inverseRest * e.panel.closedTransform;
}
std::vector<DoorDrawPose> DoorLayer::poses() const {
    std::vector<DoorDrawPose> result;
    result.reserve(impl_->active.size());
    for (const auto i : impl_->active) result.push_back({impl_->entries[i].panel.draw, transform(i)});
    return result;
}
std::vector<DoorCollider> DoorLayer::colliders(glm::vec3 minimum, glm::vec3 maximum) const {
    std::vector<DoorCollider> result;
    if (impl_->entries.empty() || !finite(minimum) || !finite(maximum) || glm::any(glm::greaterThan(minimum, maximum))) return result;
    minimum = glm::max(minimum, impl_->minimum);
    maximum = glm::min(maximum, impl_->maximum);
    if (glm::any(glm::greaterThan(minimum, maximum))) return result;
    const float reach = impl_->maximumReach;
    const int left = cell(minimum.x - reach), right = cell(maximum.x + reach);
    const int first = cell(minimum.z - reach), last = cell(maximum.z + reach);
    // For long sight queries, traverse occupied cells in the X interval rather
    // than visiting every empty cell in a potentially huge query rectangle.
    for (auto found = impl_->cells.lower_bound({left, std::numeric_limits<int>::min()});
         found != impl_->cells.end() && found->first.first <= right; ++found) {
        if (found->first.second < first || found->first.second > last) continue;
        for (const auto index : found->second) {
            const auto& e = impl_->entries[index];
            const auto& p = e.panel;
            const float swept = 2 * p.halfExtents.x + p.halfExtents.z + .02f;
            if (p.hinge.x + swept < minimum.x || p.hinge.x - swept > maximum.x ||
                p.hinge.z + swept < minimum.z || p.hinge.z - swept > maximum.z ||
                p.center.y + p.halfExtents.y + .02f < minimum.y ||
                p.center.y - p.halfExtents.y - .02f > maximum.y) continue;
            DoorCollider collider{p.center, p.halfExtents, p.rotation, index};
            if (b3Body_IsValid(e.body)) {
                const auto bodyPosition = b3Body_GetPosition(e.body);
                const auto bodyRotation = b3Body_GetRotation(e.body);
                collider.center = {bodyPosition.x, bodyPosition.y, bodyPosition.z};
                collider.rotation = {bodyRotation.s, bodyRotation.v.x, bodyRotation.v.y, bodyRotation.v.z};
            }
            result.push_back(collider);
        }
    }
    return result;
}
std::optional<DoorRayHit> DoorLayer::raycast(glm::vec3 origin, glm::vec3 direction, float reach) const {
    const float length = glm::length(direction);
    if (!finite(origin) || !finite(direction) || !std::isfinite(length) || length < 1e-6f ||
        !std::isfinite(reach) || reach <= 0 || reach > 1024) return std::nullopt;
    direction /= length;
    const auto end = origin + direction * reach;
    std::optional<DoorRayHit> closest;
    for (const auto& box : colliders(glm::min(origin, end), glm::max(origin, end))) {
        const auto inverse = glm::conjugate(box.rotation);
        b3RayCastInput ray{};
        ray.origin = vector(inverse * (origin - box.center));
        ray.translation = vector(inverse * (end - origin));
        ray.maxFraction = 1;
        const auto hull = b3MakeBoxHull(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z);
        const auto hit = b3RayCastHull(&hull.base, &ray);
        if (!hit.hit) continue;
        const float distance = hit.fraction * reach;
        if (closest && closest->distance <= distance) continue;
        const auto point = origin + direction * distance;
        closest = DoorRayHit{box.door, point, inverse * (point - box.center), distance};
    }
    return closest;
}
DoorPointState DoorLayer::pointState(std::size_t door, glm::vec3 localPoint) const {
    if (!finite(localPoint)) throw std::invalid_argument("Door point must be finite");
    const auto& entry = impl_->entries.at(door);
    if (!b3Body_IsValid(entry.body)) return {entry.panel.center + entry.panel.rotation * localPoint, glm::vec3(0)};
    const auto point = b3Body_GetWorldPoint(entry.body, vector(localPoint));
    const auto velocity = b3Body_GetWorldPointVelocity(entry.body, point);
    return {{point.x, point.y, point.z}, {velocity.x, velocity.y, velocity.z}};
}
} // namespace viewer
