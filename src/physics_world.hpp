#pragma once

#include <box3d/id.h>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
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
constexpr std::uint64_t detachedLimbCategory = 1ull << 23;
// Classification only: these shapes retain their ordinary collision category.
constexpr std::uint64_t hardImpactCategory = 1ull << 24;
// No body uses this category. Embedded props accept ray queries while their
// physical contact mask excludes the surface/bone they penetrate.
constexpr std::uint64_t embeddedPropQueryCategory = 1ull << 25;
constexpr std::uint64_t canopyBodyCategory = 1ull << 26;
class PhysicsImpacts;
class CorpseSettling;

// Shared Box3D simulation in runtime meters (X east, Y up, Z south).
// Owners of bodies/joints must retain this world and destroy their resources
// before releasing it. Advance once per application frame, then read all poses.
class PhysicsWorld {
public:
    static constexpr float fixedStepSeconds = 1.f / 60.f;
    explicit PhysicsWorld(const std::filesystem::path& acousticStorage = {});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    // Optional hooks run once per fixed step, outside Box3D callbacks.
    void step(float seconds, const std::function<void(float)>& before = {},
              const std::function<void(float)>& after = {});
    // Fraction of the next fixed step accrued since the last solved pose.
    float interpolationAlpha() const;
    b3WorldId id() const { return world_; }
    PhysicsImpacts& impacts() { return *impacts_; }
    CorpseSettling& corpseSettling() { return *corpseSettling_; }
    int allocateCollisionGroup(); // Negative groups suppress ragdoll self-contact.
    // Owner revision must have a stable address until untrackBodyMotion().
    // Movement/sleep events are consumed after every fixed step, synchronously.
    void trackBodyMotion(b3BodyId body, std::uint64_t& ownerRevision);
    void untrackBodyMotion(b3BodyId body);
    // Runs around each fixed solve, inside step()'s caller-provided hooks.
    // The finish callback may read transient contact events before callers mutate bodies.
    // Owners must unregister before destruction; callbacks must not change this list.
    void registerStepCallbacks(const void* owner, std::function<void()> prepare, std::function<void()> finish);
    void unregisterStepCallbacks(const void* owner);
    // Called synchronously when a hand acquires a prop, before the next solve.
    // Embeddings release here so even a grab-and-throw in one frame works.
    void registerGrabCallback(const void* owner, std::function<void(b3BodyId)> callback);
    void unregisterGrabCallback(const void* owner);
    bool isGrabbed(b3BodyId body) const;
private:
    friend class PhysicsGrab;
    friend class PhysicsAttachment;
    void beginGrab(b3BodyId body);
    void endGrab(b3BodyId body);
    b3WorldId world_{};
    std::unique_ptr<PhysicsImpacts> impacts_;
    std::unique_ptr<CorpseSettling> corpseSettling_;
    float accumulator_ = 0;
    int nextCollisionGroup_ = -1;
    std::unordered_map<std::uint64_t, std::uint64_t*> motionOwners_;
    struct StepCallbacks { std::function<void()> prepare, finish; };
    std::unordered_map<const void*, StepCallbacks> stepCallbacks_;
    std::unordered_map<const void*, std::function<void(b3BodyId)>> grabCallbacks_;
    std::unordered_map<std::uint64_t, unsigned> grabCounts_; // Both hands may hold one body.
};

} // namespace viewer
