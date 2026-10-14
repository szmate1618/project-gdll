#pragma once

#include <box3d/id.h>
#include <cstdint>
#include <unordered_map>

namespace viewer {

// Box3D defaults to all category bits. Assign single categories so prop-only
// meshes and queries can exclude ordinary door and ragdoll bodies.
constexpr std::uint64_t ordinaryBodyCategory = 1ull;
constexpr std::uint64_t interiorBoxCategory = 1ull << 10;
// Ragdolls retain local fallback floors; those artificial surfaces must not
// support unrelated movable props when a real scene mesh is available.
constexpr std::uint64_t ragdollFloorCategory = 1ull << 11;

// Shared Box3D simulation in runtime meters (X east, Y up, Z south).
// Owners of bodies/joints must retain this world and destroy their resources
// before releasing it. Advance once per application frame, then read all poses.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    void step(float seconds);
    b3WorldId id() const { return world_; }
    int allocateCollisionGroup(); // Negative groups suppress ragdoll self-contact.
    // Owner revision must have a stable address until untrackBodyMotion().
    // Movement/sleep events are consumed after every fixed step, synchronously.
    void trackBodyMotion(b3BodyId body, std::uint64_t& ownerRevision);
    void untrackBodyMotion(b3BodyId body);
private:
    b3WorldId world_{};
    float accumulator_ = 0;
    int nextCollisionGroup_ = -1;
    std::unordered_map<std::uint64_t, std::uint64_t*> motionOwners_;
};

} // namespace viewer
