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
constexpr float closedSpringTolerance = .001f; // Radians; keeps the spring ready for a hit at rest.
constexpr float swingAngle = 110 * B3_DEG_TO_RAD;
float flexAngle(float meters, const DoorPanel& panel) {
    return std::asin(std::min(meters / (2 * panel.halfExtents.x), 1.f));
}
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
        bool present = true, runtime = false;
        DoorRestraintState restraint;
    };
    std::shared_ptr<PhysicsWorld> world;
    DoorConfig config;
    bool replica = false;
    std::vector<Entry> entries;
    std::vector<std::size_t> active;
    std::map<std::pair<int, int>, std::vector<std::size_t>> cells;
    float maximumReach = 0;
    glm::vec3 minimum{std::numeric_limits<float>::max()}, maximum{-minimum};

    ~Impl() {
        if (world) world->unregisterStepCallbacks(this);
        for (const auto index : active) {
            b3DestroyBody(entries[index].body); // Also destroys the attached joint.
            b3DestroyBody(entries[index].anchor);
        }
    }
    void updateRestraint(Entry& e) {
        if (!b3Body_IsValid(e.body)) return;
        const float reverseLimit = flexAngle(config.breakFlexMeters > 0 ? config.breakFlexMeters :
                                             e.restraint.maxFlexMeters, e.panel);
        const float lower = e.restraint.broken || e.panel.inward ? -swingAngle : -reverseLimit;
        const float upper = e.restraint.broken || !e.panel.inward ? swingAngle : reverseLimit;
        b3RevoluteJoint_SetLimits(e.joint, lower, upper);
        const float slack = std::max(0.f, e.restraint.maxFlexMeters - config.maxClosedFlexMeters);
        b3RevoluteJoint_SetTargetAngle(e.joint, flexAngle(slack, e.panel) * (e.panel.inward ? 1.f : -1.f));
        if (e.restraint.broken) b3RevoluteJoint_EnableSpring(e.joint, false);
    }
    void prepareSprings() {
        for (const auto index : active) {
            const auto& e = entries[index];
            if (!b3Body_IsAwake(e.body)) continue;
            const float opening = b3RevoluteJoint_GetAngle(e.joint) * (e.panel.inward ? -1.f : 1.f);
            // Keep it ready at rest so contacts receive damping in their first
            // solve. Release on rebound; opened doors have no closer.
            const float slackAngle = flexAngle(std::max(0.f, e.restraint.maxFlexMeters - config.maxClosedFlexMeters), e.panel);
            const bool flexing = !e.restraint.broken && opening <= -slackAngle + closedSpringTolerance;
            if (b3RevoluteJoint_IsSpringEnabled(e.joint) != flexing)
                b3RevoluteJoint_EnableSpring(e.joint, flexing);
        }
    }
    void finishStrain() {
        if (replica || config.breakFlexMeters == 0) return;
        for (const auto index : active) {
            auto& e = entries[index];
            if (e.restraint.broken || !b3Body_IsAwake(e.body)) continue;
            const float opening = b3RevoluteJoint_GetAngle(e.joint) * (e.panel.inward ? -1.f : 1.f);
            const float stretch = 2 * e.panel.halfExtents.x * std::sin(std::clamp(-opening, 0.f, B3_PI / 2));
            // Plastic extension comes only from solved geometry. Forces and
            // hit events alone cannot damage a panel held back by another body.
            if (stretch <= e.restraint.maxFlexMeters) continue;
            e.restraint.maxFlexMeters = std::min(stretch, config.breakFlexMeters);
            e.restraint.broken = stretch >= config.breakFlexMeters - .00001f;
            if (e.restraint.broken) e.restraint.maxFlexMeters = config.breakFlexMeters;
            updateRestraint(e);
        }
    }
    Entry& activate(std::size_t index) {
        auto& e = entries.at(index);
        if (!e.present) throw std::logic_error("Door geometry is not resident");
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
        shape.filter.categoryBits = ordinaryBodyCategory | doorBodyCategory;
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
        hinge.enableSpring = true;
        hinge.targetAngle = 0;
        // The solver's angular spring mass uses inertia about the panel center;
        // the fixed edge adds leverage, so use extra damping to contain rebound.
        hinge.hertz = config.breakFlexMeters > 0 ? 10.f : 5.f;
        hinge.dampingRatio = 1.4f;
        hinge.enableLimit = true;
        e.joint = b3CreateRevoluteJoint(world->id(), &hinge);
        updateRestraint(e);
        e.inverseRest = glm::inverse(glm::translate(glm::mat4(1), e.panel.center) * glm::mat4_cast(e.panel.rotation));
        active.push_back(index);
        return e;
    }
};

DoorLayer::DoorLayer(std::vector<DoorPanel> panels, std::shared_ptr<PhysicsWorld> world, DoorConfig config, bool replica)
    : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Doors require a physics world");
    validateDoorConfig(config);
    impl_->config = config;
    impl_->replica = replica;
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
        impl_->entries.emplace_back();
        impl_->entries.back().panel = p;
        impl_->entries.back().restraint.maxFlexMeters = config.maxClosedFlexMeters;
        impl_->maximumReach = std::max(impl_->maximumReach, 2 * h.x + h.z + .02f);
        const glm::vec3 swept(2 * h.x + h.z + .02f, h.y + .02f, 2 * h.x + h.z + .02f);
        impl_->minimum = glm::min(impl_->minimum, p.hinge - swept);
        impl_->maximum = glm::max(impl_->maximum, p.hinge + swept);
    }
    impl_->world->registerStepCallbacks(impl_.get(), [state = impl_.get()] { state->prepareSprings(); },
                                       [state = impl_.get()] { state->finishStrain(); });
}
DoorLayer::~DoorLayer() = default;
std::size_t DoorLayer::count() const { return impl_->entries.size(); }
std::size_t DoorLayer::activeCount() const { return impl_->active.size(); }
const std::vector<std::size_t>& DoorLayer::activeDoors() const { return impl_->active; }
const DoorPanel& DoorLayer::panel(std::size_t door) const { return impl_->entries.at(door).panel; }

std::size_t DoorLayer::reserveRuntimeDoors(std::size_t count) {
    const auto first = impl_->entries.size();
    impl_->entries.resize(first + count);
    for (std::size_t i = first; i < first + count; ++i) {
        impl_->entries[i].present = false;
        impl_->entries[i].runtime = true;
        impl_->entries[i].restraint.maxFlexMeters = impl_->config.maxClosedFlexMeters;
    }
    return first;
}
bool DoorLayer::present(std::size_t door) const {
    return door < count() && impl_->entries[door].present;
}
void DoorLayer::installRuntimeDoor(std::size_t door, const DoorPanel& panel) {
    auto& entry = impl_->entries.at(door);
    if (!entry.runtime || entry.present) throw std::logic_error("Runtime door ID is already occupied");
    // Reuse the ordinary panel validation and conservative spatial bounds.
    DoorLayer validated({panel}, impl_->world, impl_->config, impl_->replica);
    entry.panel = panel;
    entry.present = true;
    impl_->cells[{cell(panel.hinge.x), cell(panel.hinge.z)}].push_back(door);
    impl_->maximumReach = std::max(impl_->maximumReach, validated.impl_->maximumReach);
    impl_->minimum = glm::min(impl_->minimum, validated.impl_->minimum);
    impl_->maximum = glm::max(impl_->maximum, validated.impl_->maximum);
}
void DoorLayer::evictRuntimeDoor(std::size_t door) {
    auto& entry = impl_->entries.at(door);
    if (!entry.runtime) throw std::logic_error("Cannot evict an exterior door");
    if (!entry.present) return;
    if (b3Body_IsValid(entry.body)) {
        b3DestroyBody(entry.body);
        b3DestroyBody(entry.anchor);
    }
    impl_->active.erase(std::remove(impl_->active.begin(), impl_->active.end(), door), impl_->active.end());
    const auto key = std::make_pair(cell(entry.panel.hinge.x), cell(entry.panel.hinge.z));
    auto found = impl_->cells.find(key);
    auto& ids = found->second;
    ids.erase(std::remove(ids.begin(), ids.end(), door), ids.end());
    if (ids.empty()) impl_->cells.erase(found);
    const auto restraint = entry.restraint;
    entry = {};
    entry.restraint = restraint;
    entry.present = false;
    entry.runtime = true;
}

void DoorLayer::activateNear(glm::vec3 p, float radius) {
    if (!finite(p) || !std::isfinite(radius) || radius < 0 || radius > 1024)
        throw std::invalid_argument("Invalid door activation region");
    const int left = cell(p.x - radius), right = cell(p.x + radius);
    const int first = cell(p.z - radius), last = cell(p.z + radius);
    for (int x = left; x <= right; ++x) for (int z = first; z <= last; ++z) {
        const auto found = impl_->cells.find({x, z});
        if (found == impl_->cells.end()) continue;
        for (const auto index : found->second) {
            if (!impl_->entries[index].present) continue;
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
void DoorLayer::setAngle(std::size_t door, float radians) {
    if (!std::isfinite(radians) || std::abs(radians) > 4)
        throw std::invalid_argument("Invalid replicated door angle");
    auto& e = impl_->activate(door);
    const auto turn = glm::angleAxis(-radians, glm::vec3(0, 1, 0));
    const auto p = e.panel.hinge + turn * (e.panel.center - e.panel.hinge);
    b3Body_SetTransform(e.body, position(p), rotation(turn * e.panel.rotation));
}
DoorRestraintState DoorLayer::restraint(std::size_t door) const {
    return impl_->entries.at(door).restraint;
}
void DoorLayer::setRestraint(std::size_t door, DoorRestraintState state) {
    if (!std::isfinite(state.maxFlexMeters) || state.maxFlexMeters < 0)
        throw std::invalid_argument("Invalid replicated door restraint");
    auto& e = impl_->entries.at(door);
    state.maxFlexMeters = std::max(e.restraint.maxFlexMeters, state.maxFlexMeters);
    state.broken = state.broken || e.restraint.broken;
    if (e.restraint.maxFlexMeters == state.maxFlexMeters && e.restraint.broken == state.broken) return;
    e.restraint = state;
    impl_->updateRestraint(e);
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
    for (const auto i : impl_->active)
        if (!impl_->entries[i].runtime) result.push_back({impl_->entries[i].panel.draw, transform(i)});
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
    const auto append=[&](const std::vector<std::size_t>& indices) {
        for (const auto index : indices) {
            const auto& e = impl_->entries[index];
            if (!e.present) continue;
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
    };
    const double cellCount=(double(right)-left+1)*(double(last)-first+1);
    if (cellCount<=16) {
        // Body queries cover only a handful of cells. Looking those up directly
        // avoids scanning an entire occupied X strip for every zombie step.
        for(int x=left;x<=right;++x) for(int z=first;z<=last;++z) {
            const auto found=impl_->cells.find({x,z});
            if(found!=impl_->cells.end()) append(found->second);
        }
    } else {
        // Long sight/selection queries retain the sparse occupied-cell scan.
        for (auto found=impl_->cells.lower_bound({left,std::numeric_limits<int>::min()});
             found!=impl_->cells.end() && found->first.first<=right;++found) {
            if(found->first.second>=first && found->first.second<=last) append(found->second);
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
