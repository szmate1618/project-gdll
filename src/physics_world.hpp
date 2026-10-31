#pragma once

#include <box3d/id.h>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace viewer {

// Box3D defaults to all category bits. Explicit categories isolate prop meshes
// and queries. Doors also carry a dedicated bit for living zombie blockers.
constexpr std::uint64_t ordinaryBodyCategory = 1ull;
constexpr std::uint64_t interiorBoxCategory = 1ull << 10;
// Local fallback floors support only their owning ragdoll, including severed
// parts. Their positive group matches the owner's negative collision group.
constexpr std::uint64_t ragdollFloorCategory = 1ull << 11;
constexpr std::uint64_t doorBodyCategory = 1ull << 12;
constexpr std::uint64_t zombieDoorBlockerCategory = 1ull << 13;
constexpr std::uint64_t grenadeBodyCategory = 1ull << 14;
constexpr std::uint64_t zombieCavityCategory = 1ull << 15;
constexpr std::uint64_t zombieContentsCategory = 1ull << 16;
constexpr std::uint64_t ragdollBodyCategory = 1ull << 17;
constexpr std::uint64_t vehicleBodyCategory = 1ull << 18;
constexpr std::uint64_t playerBodyCategory = 1ull << 20;
constexpr std::uint64_t meleeZombieCategory = 1ull << 21;
constexpr std::uint64_t zombieAttackBodyCategory = 1ull << 22;

// Shared Box3D simulation in runtime meters (X east, Y up, Z south).
// Owners of bodies/joints must retain this world and destroy their resources
// before releasing it. Advance once per application frame, then read all poses.
class PhysicsWorld {
public:
    static constexpr float fixedStepSeconds = 1.f / 60.f;
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    // Optional hooks run once per fixed step, outside Box3D callbacks.
    void step(float seconds, const std::function<void(float)>& before = {},
              const std::function<void(float)>& after = {});
    // Fraction of the next fixed step accrued since the last solved pose.
    float interpolationAlpha() const;
    b3WorldId id() const { return world_; }
    int allocateCollisionGroup(); // Negative groups suppress ragdoll self-contact.
    // Owner revision must have a stable address until untrackBodyMotion().
    // Movement/sleep events are consumed after every fixed step, synchronously.
    void trackBodyMotion(b3BodyId body, std::uint64_t& ownerRevision);
    void untrackBodyMotion(b3BodyId body);
    bool isGrabbed(b3BodyId body) const;
private:
    friend class PhysicsGrab;
    void beginGrab(b3BodyId body);
    void endGrab(b3BodyId body);
    b3WorldId world_{};
    float accumulator_ = 0;
    int nextCollisionGroup_ = -1;
    std::unordered_map<std::uint64_t, std::uint64_t*> motionOwners_;
    std::unordered_map<std::uint64_t, unsigned> grabCounts_; // Both hands may hold one body.
};

} // namespace viewer
