#pragma once

#include "muzzle_flash.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace viewer {
class PhysicsWorld;
class PhysicsScene;
class CollisionWorld;
class DoorLayer;
class ZombieLayer;
struct GrenadeConfig {
    float fuseSeconds = 3, blastRadius = 8, throwSpeed = 14;
};
struct GrenadePose {
    std::uint32_t id = 0;
    glm::vec3 center{0};
    glm::quat rotation{1, 0, 0, 0};
};
struct GrenadeExplosion {
    std::uint32_t id = 0;
    glm::vec3 position{0};
    float age = 0; // Seconds since detonation, for snapshots and light decay.
};
MuzzleFlashLight grenadeExplosionLight(const GrenadeExplosion& explosion);

// Transient rigid bodies in the same Box3D world as doors, boxes and ragdolls.
// Centers/poses use world meters. Only the owner advances physics, once/frame.
class Grenades {
public:
    explicit Grenades(std::shared_ptr<PhysicsWorld> physics);
    ~Grenades();
    Grenades(const Grenades&) = delete;
    Grenades& operator=(const Grenades&) = delete;
    static constexpr float radius = .045f, halfHeight = .075f;
    static constexpr std::size_t maxActive = 64;
    static constexpr std::size_t maxEvents = 128;
    bool canThrow(glm::vec3 eyes, const CollisionWorld& world) const;
    // Launch near the eyes, without spawning through a wall; inherits walking velocity.
    bool throwFrom(glm::vec3 eyes, glm::vec3 direction, glm::vec3 playerVelocity,
                   double seconds, const GrenadeConfig& config, const CollisionWorld& world);
    void prepare(PhysicsScene& scene, DoorLayer& doors); // Before the shared step.
    // After physics: resolve approximate sphere/trunk overlaps, remove expired
    // bodies, apply blast effects and publish events. Fast throws can miss trunks.
    void update(double seconds, const CollisionWorld& world, ZombieLayer* zombies = nullptr,
                double animationSeconds = 0);
    std::vector<GrenadePose> poses() const;
    std::vector<GrenadeExplosion> explosions(double seconds) const;
    std::vector<MuzzleFlashLight> lights(double seconds) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
