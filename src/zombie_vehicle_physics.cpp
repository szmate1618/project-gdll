#include "zombie_vehicle_physics.hpp"
#include "zombie_layer.hpp"
#include "zombie_chase.hpp"
#include "zombie_movement.hpp"
#include "zombie_navigation.hpp"
#include "vehicle_layer.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include "door_layer.hpp"
#include "character_step.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr float radius = ZombieCrowd::bodyRadius, height = ZombieCrowd::bodyHeight;
constexpr float mass = 75.f, activationRadius = 10.f, retentionRadius = 14.f;
constexpr float maxWalkingForce = mass * 6.f, velocityResponseSeconds = .06f;
constexpr float groupReach = radius * 2 + .1f;
glm::vec3 vector(b3Vec3 v) { return {v.x, v.y, v.z}; }
glm::vec3 feet(b3BodyId body) {
    const auto p = b3Body_GetPosition(body); return {p.x, p.y, p.z};
}
bool living(const ZombieLayer& z, std::size_t id) {
    return id < z.instances.size() && !z.instances[id].removed && !z.instances[id].ragdoll &&
           z.instances[id].asset < z.assets.size();
}
}
struct ZombieVehiclePhysics::Impl {
    struct Entry {
        b3BodyId body{};
        float outsideSeconds = 0;
        float stepSeconds = 0, stepTargetY = 0;
    };
    std::shared_ptr<PhysicsWorld> world;
    std::map<std::size_t, Entry> bodies;
    std::vector<std::size_t> candidates, neighbors, queue;
    std::set<std::size_t> selected;
    std::vector<b3ContactData> contacts;
    ~Impl() { for (const auto& pair : bodies) b3DestroyBody(pair.second.body); }
};
ZombieVehiclePhysics::ZombieVehiclePhysics(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Living zombie physics requires a shared world");
    impl_->world = std::move(world);
}
ZombieVehiclePhysics::~ZombieVehiclePhysics() = default;
std::size_t ZombieVehiclePhysics::count() const { return impl_->bodies.size(); }
glm::vec3 ZombieVehiclePhysics::release(ZombieLayer& zombies, std::size_t id) {
    auto& data = *impl_;
    const auto found = data.bodies.find(id);
    if (found == data.bodies.end()) return {};
    const auto velocity = vector(b3Body_GetLinearVelocity(found->second.body));
    b3DestroyBody(found->second.body);
    data.bodies.erase(found);
    if (id < zombies.instances.size()) {
        zombies.instances[id].vehiclePhysics = false;
        zombies.instances[id].physicsWishVelocity = {};
    }
    return velocity;
}
void ZombieVehiclePhysics::clear(ZombieLayer& zombies) {
    while (!impl_->bodies.empty()) release(zombies, impl_->bodies.begin()->first);
}
void ZombieVehiclePhysics::prepare(ZombieLayer& zombies, VehicleLayer& cars, PhysicsScene& scene,
                                   const CollisionWorld& collision, float seconds, DoorLayer* doors) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    zombies.ensureSpatialIndex();
    auto& data = *impl_;
    data.selected.clear(); data.queue.clear();
    const auto select = [&](std::size_t id) {
        if (living(zombies, id) && data.selected.insert(id).second) data.queue.push_back(id);
    };
    // Anticipate fast approaches before contact, independently of visibility or AI.
    for (const auto car : cars.simulatedCars()) {
        const auto travel = cars.velocity(car) * .35f;
        zombies.queryLivingNear(cars.pose(car).chassis.center + travel * .5f,
                                activationRadius + glm::length(travel) * .5f, data.candidates);
        for (const auto id : data.candidates) select(id);
    }
    // Hysteresis keeps pushed/airborne bodies physical until they can safely
    // return to terrain-following locomotion, avoiding activation churn.
    for (auto& [id, entry] : data.bodies) {
        if (!living(zombies, id)) continue;
        const auto p = feet(entry.body);
        bool nearby = false;
        for (const auto car : cars.simulatedCars())
            nearby |= glm::distance(p, cars.pose(car).chassis.center) < retentionRadius;
        const auto v = vector(b3Body_GetLinearVelocity(entry.body));
        const auto wanted = zombies.instances[id].physicsWishVelocity;
        const auto ground = collision.walkingGroundAt(p.x, p.z, p.y - .15f, p.y + .05f);
        const bool settled = ground && ground->normal.y >= .70710678f && std::abs(ground->height - p.y) < .1f &&
            std::abs(v.y) < .2f && glm::length(glm::vec2(v.x - wanted.x, v.z - wanted.z)) < .35f;
        entry.outsideSeconds = nearby || !settled ? 0 : entry.outsideSeconds + seconds;
        if (entry.outsideSeconds < .5f) select(id);
    }
    // Include the entire touching group. A front row must transmit pressure to
    // its neighbors even when they extend beyond the initial interest radius.
    for (std::size_t i = 0; i < data.queue.size(); ++i) {
        const auto id = data.queue[i];
        zombies.crowd_.queryNear(glm::vec3(zombies.instances[id].restRoot[3]), groupReach, data.neighbors);
        for (const auto neighbor : data.neighbors) select(neighbor);
    }
    for (auto it = data.bodies.begin(); it != data.bodies.end();) {
        const auto id = it++->first;
        if (!data.selected.count(id)) release(zombies, id);
    }
    for (const auto id : data.selected) {
        auto& instance = zombies.instances[id];
        auto it = data.bodies.find(id);
        if (it == data.bodies.end()) {
            const auto p = glm::vec3(instance.restRoot[3]);
            auto body = b3DefaultBodyDef(); body.type = b3_dynamicBody; body.position = {p.x, p.y, p.z};
            body.motionLocks.angularX = body.motionLocks.angularY = body.motionLocks.angularZ = true;
            body.enableContactRecycling = false; // Avoid ghost contacts on character capsules.
            const auto handle = b3CreateBody(data.world->id(), &body);
            auto shape = b3DefaultShapeDef();
            shape.filter.categoryBits = livingZombieCategory;
            shape.filter.maskBits = ordinaryBodyCategory | vehicleBodyCategory | livingZombieCategory | interiorBoxCategory |
                                    grenadeBodyCategory | ragdollBodyCategory | zombieContentsCategory;
            constexpr float pi = 3.14159265f;
            shape.density = mass / (pi * radius * radius * (height - 2 * radius) + 4.f / 3 * pi * radius * radius * radius);
            shape.baseMaterial.friction = .15f; shape.baseMaterial.restitution = 0;
            const b3Capsule capsule{{0, radius, 0}, {0, height - radius, 0}, radius};
            b3CreateCapsuleShape(handle, &shape, &capsule);
            try { it = data.bodies.emplace(id, Impl::Entry{handle}).first; }
            catch (...) { b3DestroyBody(handle); throw; }
            instance.vehiclePhysics = true;
            // Preserve a moving character's velocity at takeover.
            b3Body_SetLinearVelocity(handle, {instance.physicsWishVelocity.x, 0, instance.physicsWishVelocity.z});
        }
        const auto p = feet(it->second.body);
        if (doors) doors->activateNear(p, 4);
        const auto velocity = vector(b3Body_GetLinearVelocity(it->second.body));
        const auto travel = velocity * .25f;
        scene.ensureNear(p + glm::min(travel, glm::vec3(0)) - glm::vec3(2),
                         p + glm::max(travel, glm::vec3(0)) + glm::vec3(2, height + 2, 2));
        // A bounded horizontal motor expresses walking/balance effort; it never
        // overwrites collision velocity or supplies airborne locomotion.
        const int capacity = b3Body_GetContactCapacity(it->second.body);
        data.contacts.resize(capacity);
        const int count = b3Body_GetContactData(it->second.body, data.contacts.data(), capacity);
        bool supported = false, stepSupport = false, blocked = false;
        for (int c = 0; c < count; ++c) {
            const auto& contact = data.contacts[c];
            const bool isB = B3_ID_EQUALS(b3Shape_GetBody(contact.shapeIdB), it->second.body);
            for (int m = 0; m < contact.manifoldCount; ++m) {
                const auto& manifold = contact.manifolds[m];
                const auto normal = vector(manifold.normal) * (isB ? 1.f : -1.f);
                for (int k = 0; k < manifold.pointCount; ++k) {
                    if (manifold.points[k].totalNormalImpulse <= 0) continue;
                    supported |= normal.y >= .7f;
                    stepSupport |= normal.y > .05f;
                    blocked |= glm::dot(normal, instance.physicsWishVelocity) < -.05f;
                }
            }
        }
        auto& entry = it->second;
        const float desiredSpeed = glm::length(instance.physicsWishVelocity);
        if (entry.stepSeconds > 0) {
            entry.stepSeconds = std::max(0.f, entry.stepSeconds - seconds);
            if (p.y >= entry.stepTargetY - .001f || desiredSpeed < .01f) entry.stepSeconds = 0;
        } else if (stepSupport && blocked && desiredSpeed > .01f) {
            // Keep the existing curb/step clearance checks. The physical motor
            // supplies bounded lift, never a transform or a direct teleport.
            const CharacterStepConfig config{radius, height, .30f, .70710678f, .002f, .05f};
            const auto step = tryCharacterStep(p, p + instance.physicsWishVelocity * seconds, collision, config);
            if (step && step->feet.y > p.y + .001f) {
                entry.stepTargetY = step->feet.y + .005f;
                entry.stepSeconds = .4f;
            }
        }
        if (supported || entry.stepSeconds > 0) {
            auto force = (instance.physicsWishVelocity - glm::vec3(velocity.x, 0, velocity.z)) * (mass / velocityResponseSeconds);
            const float magnitude = glm::length(force);
            if (magnitude > maxWalkingForce) force *= maxWalkingForce / magnitude;
            // Let idle capsules sleep; ground friction settles tiny residual motion.
            if (desiredSpeed > .01f || glm::length(glm::vec2(velocity.x, velocity.z)) > .05f)
                b3Body_ApplyForceToCenter(it->second.body, {force.x, 0, force.z}, true);
        }
        if (entry.stepSeconds > 0) {
            const float targetSpeed = std::min(1.2f, (entry.stepTargetY - p.y) * 12.f);
            const float lift = mass * std::clamp(9.8f + (targetSpeed - velocity.y) / velocityResponseSeconds, 0.f, 25.f);
            b3Body_ApplyForceToCenter(it->second.body, {0, lift, 0}, true);
        }
    }
}
void ZombieVehiclePhysics::finish(ZombieLayer& zombies, VehicleLayer& cars, float seconds, double animationSeconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    auto& data = *impl_;
    std::vector<std::size_t> knockdowns;
    for (const auto& [id, entry] : data.bodies) {
        if (!living(zombies, id)) continue;
        auto& instance = zombies.instances[id];
        const auto before = glm::vec3(instance.restRoot[3]);
        const bool wasWalking = instance.walking, wasRunning = instance.running;
        const auto beforeAnimation = instance.walkSeconds;
        const auto p = feet(entry.body);
        const float speed = glm::length(instance.physicsWishVelocity);
        if (speed > 1e-4f) {
            const auto direction = glm::vec2(instance.physicsWishVelocity.x, instance.physicsWishVelocity.z) / speed;
            applyZombieLocomotion(instance,direction*speed,
                {p,glm::length(glm::vec2(p.x-before.x,p.z-before.z))},speed,seconds);
        } else if (p != before) {
            const auto translation = glm::translate(glm::mat4(1), p - before);
            instance.restRoot = translation * instance.restRoot;
            instance.restTransform = translation * instance.restTransform;
            instance.transform = translation * instance.transform;
        }
        if (zombies.navigation_) zombies.navigation_->recordMovement(id,before,p,seconds);
        if (p != before) zombies.updateSpatialMembership(id);
        if (p != before || wasWalking != instance.walking || wasRunning != instance.running || beforeAnimation != instance.walkSeconds)
            zombies.markRenderChanged(id, p != before);
        data.contacts.resize(b3Body_GetContactCapacity(entry.body));
        const int count = b3Body_GetContactData(entry.body, data.contacts.data(), static_cast<int>(data.contacts.size()));
        bool knockDown = false;
        for (int c = 0; c < count; ++c) {
            const auto& contact = data.contacts[c];
            const bool isB = B3_ID_EQUALS(b3Shape_GetBody(contact.shapeIdB), entry.body);
            const auto other = b3Shape_GetBody(isB ? contact.shapeIdA : contact.shapeIdB);
            const auto car = cars.carForBody(other);
            if (!car) continue;
            for (int m = 0; m < contact.manifoldCount; ++m) {
                const auto& manifold = contact.manifolds[m];
                const auto normal = vector(manifold.normal) * (isB ? 1.f : -1.f);
                float impulse = 0;
                for (int k = 0; k < manifold.pointCount; ++k) {
                    const auto& point = manifold.points[k];
                    impulse += point.totalNormalImpulse;
                    knockDown |= point.totalNormalImpulse > 0 && point.normalVelocity <= -5.f;
                }
                cars.recordResistanceImpulse(*car, -normal * impulse);
            }
        }
        if (knockDown) knockdowns.push_back(id);
    }
    for (const auto id : knockdowns) {
        // activateRagdoll removes the capsule and inherits its solved velocity.
        // Contact reaction already acted on the car; do not apply it again.
        zombies.knockDown(id, {0, .8f, 0}, animationSeconds);
    }
}
} // namespace viewer
