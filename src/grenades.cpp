#include "grenades.hpp"
#include "physics_world.hpp"
#include "physics_impacts.hpp"
#include "physics_scene.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "zombie_layer.hpp"
#include "zombie_melee_query.hpp"
#include "vehicle_layer.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
constexpr float collisionRadius = .09f, restitution = .35f;
b3Vec3 vec(glm::vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vec(b3Vec3 p) { return {p.x, p.y, p.z}; }
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

glm::mat4 bodyTransform(b3BodyId body) {
    const auto rotation = b3Body_GetRotation(body);
    return glm::translate(glm::mat4(1),vec(b3Body_GetPosition(body))) *
        glm::mat4_cast(glm::quat(rotation.s,rotation.v.x,rotation.v.y,rotation.v.z));
}

std::optional<glm::vec3> bottleZombieContact(ZombieLayer& zombies, const glm::mat4& from,
        const glm::mat4& to, double seconds, const CollisionWorld& world) {
    // A box encloses the rotating octagonal bottle. Sweep its full volume so
    // fast throws and glancing contacts do not depend on end-of-step overlap.
    std::array<glm::vec3,8> corners;
    for (int i=0; i<8; ++i)
        corners[std::size_t(i)] = {(i&1 ? 1.f : -1.f)*Grenades::radius,
            (i&2 ? 1.f : -1.f)*Grenades::halfHeight*2,(i&4 ? 1.f : -1.f)*Grenades::radius};
    const auto sweep = meleeSweep(corners.data(),int(corners.size()),from,to);
    const auto center = (sweep.bounds.minimum+sweep.bounds.maximum)*.5f;
    std::vector<std::size_t> candidates;
    zombies.visitReplicationNear(center,glm::length(sweep.bounds.maximum-center),[&](std::size_t id) {
        const auto& bounds = zombies.replicationBounds(id);
        if (!glm::any(glm::greaterThan(sweep.bounds.minimum,bounds.maximum)) &&
            !glm::any(glm::lessThan(sweep.bounds.maximum,bounds.minimum))) candidates.push_back(id);
        return true;
    });
    ZombieMeleeQuery query(&zombies,candidates,seconds);
    std::optional<MeleeSweepHit> nearest;
    for (const auto& contact : query.cast(sweep,false)) {
        // Ordinary surfaces, without the bullet joint-hit precedence. Physics
        // already handles corpse bodies. Scene cover must still block hits.
        if (!contact.body || (nearest && contact.hit.fraction>=nearest->fraction) ||
            !world.lineOfSight(glm::vec3(from[3]),contact.hit.point)) continue;
        nearest = contact.hit;
    }
    return nearest ? std::optional<glm::vec3>{nearest->point} : std::nullopt;
}

void reboundFromTrunks(b3BodyId body, const CollisionWorld& world) {
    auto position = vec(b3Body_GetPosition(body));
    auto contact = world.trunkContact(position, collisionRadius);
    if (!contact) return;
    auto velocity = vec(b3Body_GetLinearVelocity(body));
    bool bounced = false;
    // A conservative 9 cm sphere covers the rotating grenade. Resolve only
    // current overlaps, with a bounded retry for neighboring trunks. This is
    // deliberately cheaper than continuous collision and can miss fast throws.
    for (int attempt = 0; contact && attempt < 4; ++attempt) {
        position += contact->normal * (contact->depth + .001f);
        const float incoming = glm::dot(velocity, contact->normal);
        if (incoming < 0) {
            // Trunk sides have a horizontal normal, preserving vertical and
            // tangent velocity. Rounded endcaps use the full 3D normal.
            velocity -= (1 + restitution) * incoming * contact->normal;
            bounced = true;
        }
        contact = world.trunkContact(position, collisionRadius);
    }
    b3Body_SetTransform(body, vec(position), b3Body_GetRotation(body));
    if (bounced) b3Body_SetLinearVelocity(body, vec(velocity));
}

void blastBodies(PhysicsWorld& physics, const CollisionWorld& world, glm::vec3 center, float radius,
                 VehicleLayer* vehicles) {
    struct Hit {
        b3BodyId body;
        std::optional<std::size_t> car;
        glm::vec3 point, impulse;
    };
    struct Blast {
        const CollisionWorld& world;
        VehicleLayer* vehicles;
        glm::vec3 center;
        float radius;
        std::set<std::uint64_t> visited;
        std::set<std::size_t> visitedCars;
        std::vector<Hit> hits;
    } blast{world, vehicles, center, radius, {}, {}, {}};
    auto filter = b3DefaultQueryFilter();
    filter.maskBits &= ~ragdollFloorCategory;
    const auto lo = center - glm::vec3(radius), hi = center + glm::vec3(radius);
    b3World_OverlapAABB(physics.id(), {vec(lo), vec(hi)}, filter, [](b3ShapeId shape, void* context) {
        auto& b = *static_cast<Blast*>(context);
        const auto body = b3Shape_GetBody(shape);
        const auto car = b.vehicles ? b.vehicles->carForBody(body) : std::optional<std::size_t>{};
        if (car && b.visitedCars.count(*car)) return true;
        if (!car && (b3Body_GetType(body) != b3_dynamicBody || b.visited.count(b3StoreBodyId(body)))) return true;
        const auto point = car ? b.vehicles->pose(*car).chassis.center : vec(b3Body_GetPosition(body));
        const auto offset = point - b.center;
        const float distance = glm::length(offset);
        const auto surface = b3Shape_GetClosestPoint(shape, vec(b.center));
        if (distance >= b.radius || !b.world.lineOfSight(b.center, vec(surface))) return true;
        const auto outward = offset + glm::vec3(0, .35f, 0);
        const auto direction = glm::length(outward) > .001f ? glm::normalize(outward) : glm::vec3(0, 1, 0);
        const float mass = car ? b.vehicles->config(*car).weightKg : b3Body_GetMass(body);
        const float impulse = mass * 8.0f * (1 - distance / b.radius);
        if (car) b.visitedCars.insert(*car);
        else b.visited.insert(b3StoreBodyId(body));
        b.hits.push_back({body, car, point, direction * impulse});
        return true;
    }, &blast);
    // Activation replaces shapes and creates wheels. Finish the spatial query
    // first, and apply one total-mass impulse per car, regardless of body count.
    for (const auto& hit : blast.hits) {
        if (hit.car) vehicles->applyImpulse(*hit.car, hit.impulse, hit.point);
        else b3Body_ApplyLinearImpulse(hit.body, vec(hit.impulse), vec(hit.point), true);
    }
}
}

struct Grenades::Impl {
    struct Entry {
        std::uint32_t id; b3BodyId body; double explodeAt; float blastRadius;
        GrenadeKind kind = GrenadeKind::he;
        glm::vec3 incoming{0,-1,0};
        std::optional<MolotovImpact> impact;
        glm::mat4 stepStart{1};
        struct Flight { glm::mat4 from, to; glm::vec3 incoming; };
        std::vector<Flight> flight{};
    };
    struct Event { std::uint32_t id; glm::vec3 position; double seconds; };
    std::shared_ptr<PhysicsWorld> physics;
    b3HullData* hull = nullptr;
    b3HullData* bottleHull = nullptr;
    std::vector<Entry> active;
    std::vector<Event> events;
    struct BottleEvent { MolotovImpact impact; double seconds; };
    std::vector<BottleEvent> bottleEvents;
    std::uint32_t nextId = 1;
    void captureVelocities() {
        for (auto& g : active) if (g.kind==GrenadeKind::molotov && !g.impact) {
            g.stepStart = bodyTransform(g.body);
            const auto velocity = vec(b3Body_GetLinearVelocity(g.body));
            if (glm::length(velocity)>.001f) g.incoming = glm::normalize(velocity);
        }
    }
    void captureImpacts() {
        for (auto& g : active) if (g.kind==GrenadeKind::molotov && !g.impact)
            g.flight.push_back({g.stepStart,bodyTransform(g.body),g.incoming});
        const auto contacts = b3World_GetContactEvents(physics->id());
        for (int i=0; i<contacts.hitCount; ++i) {
            const auto& hit = contacts.hitEvents[i];
            if (!b3Shape_IsValid(hit.shapeIdA) || !b3Shape_IsValid(hit.shapeIdB)) continue;
            const auto a = b3Shape_GetBody(hit.shapeIdA), b = b3Shape_GetBody(hit.shapeIdB);
            for (auto& g : active) if (g.kind==GrenadeKind::molotov && !g.impact &&
                    (B3_ID_EQUALS(g.body,a) || B3_ID_EQUALS(g.body,b)))
                g.impact = MolotovImpact{g.id,{float(hit.point.x),float(hit.point.y),float(hit.point.z)},g.incoming,0};
        }
    }
    ~Impl() {
        physics->unregisterStepCallbacks(this);
        for (const auto& grenade : active) {
            physics->impacts().untrack(grenade.body);
            b3DestroyBody(grenade.body);
        }
        if (hull) b3DestroyHull(hull);
        if (bottleHull) b3DestroyHull(bottleHull);
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
    for (auto& p : points) p.y *= 2;
    impl_->bottleHull = b3CreateHull(points.data(),static_cast<int>(points.size()),static_cast<int>(points.size()));
    if (!impl_->bottleHull) throw std::runtime_error("Cannot build bottle collider");
    auto* owner = impl_.get();
    impl_->physics->registerStepCallbacks(owner,[owner] { owner->captureVelocities(); },[owner] { owner->captureImpacts(); });
}
Grenades::~Grenades() = default;

bool Grenades::canThrow(glm::vec3 eyes, const CollisionWorld& world, GrenadeKind kind) const {
    const float clearance = kind==GrenadeKind::molotov ? .16f : collisionRadius;
    // A conservative sphere fits every orientation of the 9 x 15 cm cylinder.
    return finite(eyes) && impl_->active.size() < maxActive && impl_->nextId != 0 &&
        world.contacts(eyes - glm::vec3(0, clearance, 0), clearance, clearance * 2).empty();
}
bool Grenades::throwFrom(glm::vec3 eyes, glm::vec3 direction, glm::vec3 playerVelocity,
                         double seconds, const GrenadeConfig& config, const CollisionWorld& world, float power) {
    if (config.kind>GrenadeKind::molotov || !canThrow(eyes, world, config.kind) || !finite(direction) || !finite(playerVelocity) || glm::length(direction) < .001f ||
        !std::isfinite(seconds) || !std::isfinite(config.fuseSeconds) || config.fuseSeconds <= 0 ||
        !std::isfinite(config.blastRadius) || (config.kind==GrenadeKind::he && config.blastRadius <= 0) ||
        !std::isfinite(config.throwSpeed) || config.throwSpeed <= 0 ||
        !std::isfinite(power) || power < 0 || power > 1) return false;
    auto definition = b3DefaultBodyDef();
    definition.type = b3_dynamicBody;
    // Starting at the clear eye position prevents throwing through nearby walls.
    definition.position = {eyes.x, eyes.y, eyes.z};
    definition.linearVelocity = vec((glm::normalize(direction) * config.throwSpeed + glm::vec3(0, 2, 0)) * power + playerVelocity);
    definition.angularVelocity = {8 * power, 2 * power, 3 * power};
    definition.isBullet = true;
    definition.linearDamping = .08f; definition.angularDamping = .2f;
    const auto body = b3CreateBody(impl_->physics->id(), &definition);
    auto shape = b3DefaultShapeDef();
    shape.density = .45f / (8 * .5f * radius * radius * std::sin(glm::radians(45.0f)) * halfHeight * 2);
    shape.baseMaterial.friction = .5f;
    shape.baseMaterial.restitution = config.kind==GrenadeKind::he ? restitution : 0;
    shape.filter.categoryBits = grenadeBodyCategory;
    shape.filter.maskBits &= ~ragdollFloorCategory;
    b3CreateHullShape(body, &shape, config.kind==GrenadeKind::he ? impl_->hull : impl_->bottleHull);
    try { impl_->physics->impacts().track(body, config.kind==GrenadeKind::he ? "he_grenade" : "molotov"); }
    catch (...) { b3DestroyBody(body); throw; }
    try { impl_->active.push_back({impl_->nextId++, body, seconds + config.fuseSeconds, config.blastRadius,config.kind,glm::normalize(direction),{}}); }
    catch (...) { impl_->physics->impacts().untrack(body); b3DestroyBody(body); throw; }
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
        doors.activateNear(center, glm::length(travel) + 3);
    }
}
std::vector<NoiseEvent> Grenades::update(double seconds, const CollisionWorld& world, ZombieLayer* zombies,
                                       double animationSeconds, VehicleLayer* vehicles) {
    std::vector<NoiseEvent> noises;
    if (!std::isfinite(seconds)) return noises;
    auto& events = impl_->events;
    events.erase(std::remove_if(events.begin(), events.end(), [&](const auto& e) { return seconds - e.seconds > 1; }), events.end());
    auto& bottleEvents = impl_->bottleEvents;
    bottleEvents.erase(std::remove_if(bottleEvents.begin(),bottleEvents.end(),[&](const auto& e) {
        return seconds-e.seconds>=molotovFireSeconds;
    }),bottleEvents.end());
    auto& active = impl_->active;
    for (auto it = active.begin(); it != active.end();) {
        if (it->kind==GrenadeKind::molotov) {
            const auto position = vec(b3Body_GetPosition(it->body));
            if (zombies) for (const auto& flight : it->flight) {
                if (const auto hit = bottleZombieContact(*zombies,flight.from,flight.to,animationSeconds,world)) {
                    it->impact = MolotovImpact{it->id,*hit,flight.incoming,0};
                    break;
                }
            }
            it->flight.clear();
            if (!it->impact && world.trunkContact(position,.16f))
                it->impact = MolotovImpact{it->id,position,it->incoming,0};
            if (!it->impact && seconds<it->explodeAt) { ++it; continue; }
            if (it->impact) {
                bottleEvents.push_back({*it->impact,seconds});
                if (bottleEvents.size()>maxEvents) bottleEvents.erase(bottleEvents.begin());
            }
            impl_->physics->impacts().untrack(it->body);
            b3DestroyBody(it->body);
            it = active.erase(it);
            continue;
        }
        if (world.stats().trunks) reboundFromTrunks(it->body, world);
        if (seconds < it->explodeAt) { ++it; continue; }
        const auto p = b3Body_GetPosition(it->body);
        const glm::vec3 position(p.x, p.y, p.z);
        events.push_back({it->id, position, seconds});
        if (events.size() > maxEvents) events.erase(events.begin());
        const float blastRadius = it->blastRadius;
        impl_->physics->impacts().untrack(it->body);
        b3DestroyBody(it->body);
        it = active.erase(it);
        if (zombies) {
            zombies->explode(position, blastRadius, animationSeconds, world);
        }
        blastBodies(*impl_->physics, world, position, blastRadius, vehicles);
        noises.push_back({position,130});
    }
    return noises;
}
std::vector<GrenadePose> Grenades::poses() const {
    std::vector<GrenadePose> result;
    for (const auto& grenade : impl_->active) {
        const auto p = b3Body_GetPosition(grenade.body);
        const auto q = b3Body_GetRotation(grenade.body);
        result.push_back({grenade.id, {p.x, p.y, p.z}, {q.s, q.v.x, q.v.y, q.v.z},grenade.kind});
    }
    return result;
}
std::vector<GrenadeExplosion> Grenades::explosions(double seconds) const {
    std::vector<GrenadeExplosion> result;
    for (const auto& event : impl_->events)
        result.push_back({event.id, event.position, static_cast<float>(std::max(0.0, seconds - event.seconds))});
    return result;
}
std::vector<MolotovImpact> Grenades::molotovImpacts(double seconds) const {
    std::vector<MolotovImpact> result;
    for (const auto& event : impl_->bottleEvents) {
        const double age = seconds-event.seconds;
        if (age<0 || age>=molotovFireSeconds) continue;
        auto impact = event.impact; impact.age = static_cast<float>(age);
        result.push_back(impact);
    }
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
