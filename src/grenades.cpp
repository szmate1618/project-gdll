#include "grenades.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "zombie_layer.hpp"

#include <box3d/box3d.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
b3Vec3 vec(glm::vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vec(b3Vec3 p) { return {p.x, p.y, p.z}; }
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

void blastBodies(PhysicsWorld& physics, const CollisionWorld& world, glm::vec3 center, float radius) {
    struct Blast {
        const CollisionWorld& world;
        glm::vec3 center;
        float radius;
        std::set<std::uint64_t> visited;
    } blast{world, center, radius, {}};
    auto filter = b3DefaultQueryFilter();
    filter.maskBits &= ~ragdollFloorCategory;
    const auto lo = center - glm::vec3(radius), hi = center + glm::vec3(radius);
    b3World_OverlapAABB(physics.id(), {vec(lo), vec(hi)}, filter, [](b3ShapeId shape, void* context) {
        auto& b = *static_cast<Blast*>(context);
        const auto body = b3Shape_GetBody(shape);
        if (b3Body_GetType(body) != b3_dynamicBody || !b.visited.insert(b3StoreBodyId(body)).second) return true;
        const auto point = b3Body_GetPosition(body);
        const auto offset = vec(point) - b.center;
        const float distance = glm::length(offset);
        const auto surface = b3Shape_GetClosestPoint(shape, vec(b.center));
        if (distance >= b.radius || !b.world.lineOfSight(b.center, vec(surface))) return true;
        const auto outward = offset + glm::vec3(0, .35f, 0);
        const auto direction = glm::length(outward) > .001f ? glm::normalize(outward) : glm::vec3(0, 1, 0);
        const float impulse = b3Body_GetMass(body) * 8.0f * (1 - distance / b.radius);
        b3Body_ApplyLinearImpulse(body, vec(direction * impulse), point, true);
        return true;
    }, &blast);
}
}

struct Grenades::Impl {
    struct Entry { std::uint32_t id; b3BodyId body; double explodeAt; float blastRadius; };
    struct Event { std::uint32_t id; glm::vec3 position; double seconds; };
    std::shared_ptr<PhysicsWorld> physics;
    b3HullData* hull = nullptr;
    std::vector<Entry> active;
    std::vector<Event> events;
    std::uint32_t nextId = 1;
    ~Impl() {
        for (const auto& grenade : active) b3DestroyBody(grenade.body);
        if (hull) b3DestroyHull(hull);
    }
};

MuzzleFlashLight grenadeExplosionLight(const GrenadeExplosion& explosion) {
    MuzzleFlash pulse(.18, {35, 14, 3}, 12);
    pulse.fire(0, explosion.position);
    return pulse.light(explosion.age);
}

Grenades::Grenades(std::shared_ptr<PhysicsWorld> physics) : impl_(std::make_unique<Impl>()) {
    if (!physics) throw std::invalid_argument("Grenades need a shared physics world");
    impl_->physics = std::move(physics);
    std::array<b3Vec3, 16> points{};
    for (int y = 0; y < 2; ++y) for (int side = 0; side < 8; ++side) {
        const float angle = side * glm::radians(45.0f);
        points[static_cast<std::size_t>(y * 8 + side)] = {radius * std::cos(angle), (y ? 1 : -1) * halfHeight,
                                                        radius * std::sin(angle)};
    }
    impl_->hull = b3CreateHull(points.data(), static_cast<int>(points.size()), static_cast<int>(points.size()));
    if (!impl_->hull) throw std::runtime_error("Cannot build octagonal grenade collider");
}
Grenades::~Grenades() = default;

bool Grenades::canThrow(glm::vec3 eyes, const CollisionWorld& world) const {
    // A conservative sphere fits every orientation of the 9 x 15 cm cylinder.
    constexpr float clearance = .09f;
    return finite(eyes) && impl_->active.size() < maxActive && impl_->nextId != 0 &&
        world.contacts(eyes - glm::vec3(0, clearance, 0), clearance, clearance * 2).empty();
}
bool Grenades::throwFrom(glm::vec3 eyes, glm::vec3 direction, glm::vec3 playerVelocity,
                         double seconds, const GrenadeConfig& config, const CollisionWorld& world) {
    if (!canThrow(eyes, world) || !finite(direction) || !finite(playerVelocity) || glm::length(direction) < .001f ||
        !std::isfinite(seconds) || !std::isfinite(config.fuseSeconds) || config.fuseSeconds <= 0 ||
        !std::isfinite(config.blastRadius) || config.blastRadius <= 0 ||
        !std::isfinite(config.throwSpeed) || config.throwSpeed <= 0) return false;
    auto definition = b3DefaultBodyDef();
    definition.type = b3_dynamicBody;
    // Starting at the clear eye position prevents throwing through nearby walls.
    definition.position = {eyes.x, eyes.y, eyes.z};
    definition.linearVelocity = vec(glm::normalize(direction) * config.throwSpeed + playerVelocity + glm::vec3(0, 2, 0));
    definition.angularVelocity = {8, 2, 3};
    definition.isBullet = true;
    definition.linearDamping = .08f; definition.angularDamping = .2f;
    const auto body = b3CreateBody(impl_->physics->id(), &definition);
    auto shape = b3DefaultShapeDef();
    shape.density = .45f / (8 * .5f * radius * radius * std::sin(glm::radians(45.0f)) * halfHeight * 2);
    shape.baseMaterial.friction = .5f;
    shape.baseMaterial.restitution = .35f;
    shape.filter.categoryBits = grenadeBodyCategory;
    shape.filter.maskBits &= ~ragdollFloorCategory;
    b3CreateHullShape(body, &shape, impl_->hull);
    try { impl_->active.push_back({impl_->nextId++, body, seconds + config.fuseSeconds, config.blastRadius}); }
    catch (...) { b3DestroyBody(body); throw; }
    return true;
}
void Grenades::prepare(PhysicsScene& scene, DoorLayer& doors) {
    for (const auto& g : impl_->active) {
        const auto p = b3Body_GetPosition(g.body);
        const glm::vec3 center(p.x, p.y, p.z);
        const auto travel = vec(b3Body_GetLinearVelocity(g.body)) * .2f;
        const auto minimum = center + glm::min(travel, glm::vec3(0)) - glm::vec3(2);
        const auto maximum = center + glm::max(travel, glm::vec3(0)) + glm::vec3(2);
        scene.ensureNear(minimum, maximum);
        scene.ensureTrunksNear(minimum, maximum);
        doors.activateNear(center, glm::length(travel) + 3);
    }
}
void Grenades::update(double seconds, const CollisionWorld& world, ZombieLayer* zombies, double animationSeconds) {
    if (!std::isfinite(seconds)) return;
    auto& events = impl_->events;
    events.erase(std::remove_if(events.begin(), events.end(), [&](const auto& e) { return seconds - e.seconds > 1; }), events.end());
    auto& active = impl_->active;
    for (auto it = active.begin(); it != active.end();) {
        if (seconds < it->explodeAt) { ++it; continue; }
        const auto p = b3Body_GetPosition(it->body);
        const glm::vec3 position(p.x, p.y, p.z);
        events.push_back({it->id, position, seconds});
        if (events.size() > maxEvents) events.erase(events.begin());
        const float blastRadius = it->blastRadius;
        b3DestroyBody(it->body);
        it = active.erase(it);
        if (zombies) {
            zombies->explode(position, blastRadius, animationSeconds, world);
            zombies->makeNoise(position, 130);
        }
        blastBodies(*impl_->physics, world, position, blastRadius);
    }
}
std::vector<GrenadePose> Grenades::poses() const {
    std::vector<GrenadePose> result;
    for (const auto& grenade : impl_->active) {
        const auto p = b3Body_GetPosition(grenade.body);
        const auto q = b3Body_GetRotation(grenade.body);
        result.push_back({grenade.id, {p.x, p.y, p.z}, {q.s, q.v.x, q.v.y, q.v.z}});
    }
    return result;
}
std::vector<GrenadeExplosion> Grenades::explosions(double seconds) const {
    std::vector<GrenadeExplosion> result;
    for (const auto& event : impl_->events)
        result.push_back({event.id, event.position, static_cast<float>(std::max(0.0, seconds - event.seconds))});
    return result;
}
std::vector<MuzzleFlashLight> Grenades::lights(double seconds) const {
    std::vector<MuzzleFlashLight> result;
    for (const auto& explosion : explosions(seconds)) {
        const auto light = grenadeExplosionLight(explosion);
        if (light.color.r > 0) result.push_back(light);
    }
    return result;
}
} // namespace viewer
