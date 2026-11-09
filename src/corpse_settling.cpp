#include "corpse_settling.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viewer {
namespace {
constexpr float quietSpeed = .005f; // Metres/second, including rotational point motion.
constexpr float quietSeconds = 2.f;

bool equal(b3Vec3 a, b3Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool equal(b3WorldTransform a, b3WorldTransform b) {
    return a.p.x == b.p.x && a.p.y == b.p.y && a.p.z == b.p.z &&
           equal(a.q.v, b.q.v) && a.q.s == b.q.s;
}

float motionRadius(b3BodyId body) {
    std::vector<b3ShapeId> shapes(b3Body_GetShapeCount(body));
    b3Body_GetShapes(body, shapes.data(), static_cast<int>(shapes.size()));
    float radius = 0;
    for (const auto shape : shapes) {
        float extent;
        if (b3Shape_GetType(shape) == b3_capsuleShape) {
            const auto capsule = b3Shape_GetCapsule(shape);
            extent = std::max(b3Length(capsule.center1), b3Length(capsule.center2)) + capsule.radius;
        } else if (b3Shape_GetType(shape) == b3_sphereShape) {
            const auto sphere = b3Shape_GetSphere(shape);
            extent = b3Length(sphere.center) + sphere.radius;
        } else {
            const auto box = b3Shape_GetAABB(shape);
            const auto p = b3Body_GetPosition(body);
            const b3Vec3 reach{
                static_cast<float>(std::max(std::abs(box.lowerBound.x - p.x), std::abs(box.upperBound.x - p.x))),
                static_cast<float>(std::max(std::abs(box.lowerBound.y - p.y), std::abs(box.upperBound.y - p.y))),
                static_cast<float>(std::max(std::abs(box.lowerBound.z - p.z), std::abs(box.upperBound.z - p.z)))};
            extent = b3Length(reach);
        }
        radius = std::max(radius, extent);
    }
    return radius;
}

float pointMotion(b3WorldTransform before, b3WorldTransform after, float radius) {
    // Quaternion vector difference remains accurate for very small rotations;
    // acos(dot(q0,q1)) would round them to zero. The sum bounds every shape point.
    const auto rotation = b3InvMulQuat(before.q, after.q);
    return b3Length(b3SubPos(after.p, before.p)) + 2.f * radius * b3Length(rotation.v);
}
} // namespace

struct CorpseSettling::Impl {
    struct Entry {
        b3BodyId body{};
        b3WorldTransform pose{}, start{};
        b3Vec3 velocity{}, angularVelocity{};
        float radius = 0, quietTime = 0;
        bool enabled = true, awake = false, prepared = false;
        bool blocked = true, supported = false;
        std::uint64_t updated = 0, visited = 0;
        std::vector<std::uint64_t> neighbors;
        // Separate shape and joint ID domains; changes invalidate the quiet window.
        std::vector<std::pair<bool, std::uint64_t>> connections;
    };
    explicit Impl(PhysicsWorld& owner) : world(owner) {}
    PhysicsWorld& world;
    std::unordered_map<std::uint64_t, Entry> entries;
    std::unordered_set<std::uint64_t> active;
    std::uint64_t step = 0;

    bool eligible(const Entry& entry) const {
        return b3World_IsSleepingEnabled(world.id()) && entry.enabled &&
               b3Body_GetType(entry.body) == b3_dynamicBody &&
               b3Body_IsSleepEnabled(entry.body) && !world.isGrabbed(entry.body);
    }
};

CorpseSettling::CorpseSettling(PhysicsWorld& world) : impl_(std::make_unique<Impl>(world)) {}
CorpseSettling::~CorpseSettling() = default;

void CorpseSettling::track(b3BodyId body) {
    if (!b3Body_IsValid(body) || body.world0 + 1 != impl_->world.id().index1)
        throw std::invalid_argument("Corpse settling requires a body in its physics world");
    Impl::Entry entry;
    entry.body = body;
    entry.pose = b3Body_GetTransform(body);
    entry.velocity = b3Body_GetLinearVelocity(body);
    entry.angularVelocity = b3Body_GetAngularVelocity(body);
    entry.radius = motionRadius(body);
    entry.awake = b3Body_IsAwake(body);
    const auto key = b3StoreBodyId(body);
    if (!impl_->entries.emplace(key, std::move(entry)).second)
        throw std::logic_error("Corpse body already registered for settling");
    if (b3Body_IsAwake(body)) impl_->active.insert(key);
}
void CorpseSettling::untrack(b3BodyId body) {
    const auto key = b3StoreBodyId(body);
    impl_->active.erase(key);
    impl_->entries.erase(key);
}
void CorpseSettling::invalidate(b3BodyId body) {
    const auto found = impl_->entries.find(b3StoreBodyId(body));
    if (found == impl_->entries.end()) return;
    found->second.quietTime = 0;
    if (b3Body_IsValid(body) && b3Body_IsAwake(body)) impl_->active.insert(found->first);
}
void CorpseSettling::setEnabled(b3BodyId body, bool enabled) {
    const auto found = impl_->entries.find(b3StoreBodyId(body));
    if (found == impl_->entries.end()) return;
    found->second.enabled = enabled;
    invalidate(body);
}

void CorpseSettling::prepare() {
    auto& data = *impl_;
    for (auto it = data.active.begin(); it != data.active.end();) {
        auto& entry = data.entries.at(*it);
        if (!b3Body_IsValid(entry.body)) {
            data.entries.erase(*it);
            it = data.active.erase(it);
            continue;
        }
        entry.start = b3Body_GetTransform(entry.body);
        const bool awake = b3Body_IsAwake(entry.body);
        if (!awake || !entry.awake || !equal(entry.pose, entry.start) ||
            !equal(entry.velocity, b3Body_GetLinearVelocity(entry.body)) ||
            !equal(entry.angularVelocity, b3Body_GetAngularVelocity(entry.body)) || !data.eligible(entry))
            entry.quietTime = 0;
        entry.awake = awake;
        entry.prepared = awake;
        if (!awake) {
            entry.pose = entry.start;
            it = data.active.erase(it);
        } else ++it;
    }
}

void CorpseSettling::finish() {
    auto& data = *impl_;
    if (data.entries.empty()) return;
    ++data.step;
    const auto events = b3World_GetBodyEvents(data.world.id());
    for (int i = 0; i < events.moveCount; ++i) {
        const auto& event = events.moveEvents[i];
        const auto found = data.entries.find(b3StoreBodyId(event.bodyId));
        if (found == data.entries.end()) continue;
        auto& entry = found->second;
        if (!entry.prepared) { entry.start = entry.pose; entry.quietTime = 0; }
        entry.blocked = !data.eligible(entry);
        if (event.fellAsleep || entry.blocked ||
            pointMotion(entry.start, event.transform, entry.radius) > quietSpeed * PhysicsWorld::fixedStepSeconds)
            entry.quietTime = 0;
        else entry.quietTime += PhysicsWorld::fixedStepSeconds;
        entry.pose = event.transform;
        entry.velocity = b3Body_GetLinearVelocity(entry.body);
        entry.angularVelocity = b3Body_GetAngularVelocity(entry.body);
        entry.awake = !event.fellAsleep;
        entry.prepared = false;
        entry.updated = data.step;
        if (entry.awake) data.active.insert(found->first);
        else data.active.erase(found->first);
    }

    if (!b3World_IsSleepingEnabled(data.world.id())) return;

    // Work scales with awake corpse parts. Sleeping populations are visited only
    // when native movement events wake them; terrain never joins separate piles.
    const std::vector<std::uint64_t> seeds(data.active.begin(), data.active.end());
    std::vector<b3ContactData> contacts;
    std::vector<b3JointId> joints;
    std::vector<std::pair<bool, std::uint64_t>> connections;
    for (const auto key : seeds) {
        auto& entry = data.entries.at(key);
        entry.neighbors.clear(); entry.supported = false; connections.clear();
        // Even ineligible registered bodies need their edges: skipping their
        // side of the graph could split a driven/held rig from a quiet corpse.
        if (entry.updated != data.step) { entry.blocked = true; continue; }
        const auto connect = [&](b3BodyId other) {
            if (b3Body_GetType(other) == b3_staticBody) return;
            const auto neighbor = b3StoreBodyId(other);
            if (!data.entries.count(neighbor)) entry.blocked = true;
            else entry.neighbors.push_back(neighbor);
        };
        contacts.resize(b3Body_GetContactCapacity(entry.body));
        const int contactCount = b3Body_GetContactData(entry.body, contacts.data(), static_cast<int>(contacts.size()));
        for (int c = 0; c < contactCount; ++c) {
            const auto& contact = contacts[c];
            const bool isA = B3_ID_EQUALS(b3Shape_GetBody(contact.shapeIdA), entry.body);
            const auto otherShape = isA ? contact.shapeIdB : contact.shapeIdA;
            const auto other = b3Shape_GetBody(otherShape);
            connections.emplace_back(false, b3StoreShapeId(otherShape));
            connect(other);
            if (b3Body_GetType(other) != b3_staticBody) continue;
            for (int m = 0; m < contact.manifoldCount; ++m) {
                const auto& manifold = contact.manifolds[m];
                if ((isA ? -manifold.normal.y : manifold.normal.y) < .25f) continue;
                for (int p = 0; p < manifold.pointCount; ++p)
                    if (manifold.points[p].normalImpulse > 0) entry.supported = true;
            }
        }
        joints.resize(b3Body_GetJointCount(entry.body));
        const int jointCount = b3Body_GetJoints(entry.body, joints.data(), static_cast<int>(joints.size()));
        for (int j = 0; j < jointCount; ++j) {
            const auto a = b3Joint_GetBodyA(joints[j]), b = b3Joint_GetBodyB(joints[j]);
            const auto other = B3_ID_EQUALS(a, entry.body) ? b : a;
            connections.emplace_back(true, b3StoreJointId(joints[j]));
            // Static joints are external anchors, not ground support. In
            // particular a hand/carry anchor must never authorize sleeping.
            if (b3Body_GetType(other) == b3_staticBody) entry.blocked = true;
            else connect(other);
        }
        std::sort(connections.begin(), connections.end());
        if (connections != entry.connections) {
            entry.connections = connections;
            entry.quietTime = 0;
        }
    }

    std::vector<Impl::Entry*> group;
    for (const auto seed : seeds) {
        auto& first = data.entries.at(seed);
        if (first.visited == data.step || !first.awake) continue;
        group.clear(); group.push_back(&first); first.visited = data.step;
        bool quiet = true, supported = false;
        for (std::size_t i = 0; i < group.size(); ++i) {
            auto& entry = *group[i];
            quiet = quiet && entry.updated == data.step && entry.awake && !entry.blocked &&
                    entry.quietTime + 1e-5f >= quietSeconds;
            supported = supported || entry.supported;
            for (const auto key : entry.neighbors) {
                auto& next = data.entries.at(key);
                if (next.visited == data.step) continue;
                next.visited = data.step; group.push_back(&next);
            }
        }
        if (!quiet || !supported) continue;
        b3Body_SetAwake(first.body, false);
        for (auto* entry : group) if (!b3Body_IsAwake(entry->body)) {
            entry->awake = false; entry->quietTime = 0;
            data.active.erase(b3StoreBodyId(entry->body));
        }
    }
}
} // namespace viewer
