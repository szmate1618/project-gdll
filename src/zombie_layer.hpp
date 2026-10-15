#pragma once

#include "animated_model.hpp"
#include "collision_world.hpp"
#include "zombie_behavior.hpp"
#include "zombie_spatial_index.hpp"
#include "zombie_crowd.hpp"
#include "zombie_ragdoll.hpp"
#include "zombie_hit.hpp"
#include "zombie_render_updates.hpp"

#include <memory>
#include <unordered_map>

namespace viewer {

class PhysicsWorld;

struct ZombieInstance {
    std::size_t asset = 0;
    glm::mat4 transform{1};
    // Live placement until shot; then frozen as the ragdoll handoff basis.
    glm::mat4 restTransform{1};
    glm::mat4 restRoot{1};
    // Last camera pose, published by rendering for exact hit/ragdoll handoff.
    // Derived display state only; the simulation remains single-threaded.
    mutable float displayedAnimationFrame = -1;
    mutable int displayedAnimationClip = 0; // 0 idle, 1 walk, 2 run.
    float phase = 0; // Fraction of the idle cycle, independent of frame rate.
    bool walking = false;
    bool running = false; // Select the authored run clip during pursuit.
    glm::mat4 chaseNormalization{1}; // Captured on activation; preserve asset scale/offset.
    double walkSeconds = 0; // Locomotion clip time; advances only while travelling.
    bool ragdoll = false;
    bool removed = false; // Tombstone: source IDs remain stable after corpse removal.
    std::size_t ragdollHandle = 0;
    bool physicsRagdoll = false;
    std::uint64_t physicsPoseRevision = 0;
    std::uint64_t ragdollPoseVersion = 0;
    // Skeleton world pose captured from the displayed frame, in asset space.
    std::vector<glm::mat4> activationPose;
    // Asset-space skinning palette. After initialization, mutate through the
    // layer's owning API so bounds and persistent GPU palettes are invalidated.
    std::vector<glm::mat4> ragdollBones;
};

struct ZombieBehaviorStats {
    std::size_t activationCandidates = 0, perceptionChecks = 0, sightRays = 0, hearingCandidates = 0;
};
struct ZombieShootingStats {
    std::size_t spatialCandidates = 0, poseSamples = 0, headVertices = 0;
};

struct ZombieLayer {
    ZombieLayer();
    ~ZombieLayer();
    ZombieLayer(ZombieLayer&&) noexcept;
    ZombieLayer& operator=(ZombieLayer&&) noexcept;
    ZombieLayer(const ZombieLayer&) = delete;
    ZombieLayer& operator=(const ZombieLayer&) = delete;

    std::vector<AnimatedModel> assets;
    // Per original asset, ordered LOD1..3; originals own gameplay/rig identity.
    std::vector<std::vector<AnimatedModel>> lodAssets;
    std::vector<ZombieInstance> instances;
    ZombieBehaviorPool behaviors;
    glm::vec3 boundsMin{0}, boundsMax{0};

    // Returns true when the nearest living zombie under the ray was activated.
    // animationSeconds is the time of the last displayed frame (before phase).
    bool shoot(glm::vec3 origin, glm::vec3 direction, double animationSeconds = 0.0);
    const ZombieShootingStats& shootingStats() const { return shootingStats_; }
    // Silent noise at a world position, with a source level in dB at one meter.
    // Only idle, living zombies already in the behavior pool can hear it.
    void makeNoise(glm::vec3 position, float decibels);
    // Disable only for comparison/tests of the original every-step perception.
    void setPerceptionStaggering(bool enabled);
    const ZombieBehaviorStats& behaviorStats() const { return behaviorStats_; }
    const ZombieCrowdStats& crowdStats() const { return crowd_.stats(); }
    // Rebuild after external edits to instance feet or replacing a population
    // with the same size, or edits to asset rigs/clips or instance normalization.
    // Rebuilds prepared hit geometry and corpse membership too. Call after
    // initial fixture/bulk edits to ragdoll/removed flags. Ordinary locomotion
    // and the corpse owning APIs maintain these indices incrementally.
    void rebuildSpatialIndex();
    // Perception/state update, followed by terrain-following locomotion.
    // The app advances SimulationClock before calling this once per step.
    void updateBehavior(float seconds, const ZombiePlayer& player, const CollisionWorld& world,
                        double animationSeconds = 0.0);
    // Convenience entry point for a standard 1.8 m player; target is feet.
    void chase(float seconds, glm::vec3 target, const CollisionWorld& world,
               double animationSeconds = 0.0);
    // Attach before the first ragdoll. The app then steps the shared world and
    // calls synchronizePhysics(); standalone update() retains its own stepping.
    void attachPhysics(std::shared_ptr<PhysicsWorld> world);
    void synchronizePhysics();
    void translateRagdoll(std::size_t zombie, glm::vec3 displacement);
    void applyRagdollImpulse(std::size_t zombie, RagdollPart part, glm::vec3 impulse);
    void removeRagdoll(std::size_t zombie);
    // Externally driven rigs/tests only; physics-owned palettes use the APIs above.
    void setRagdollPalette(std::size_t zombie, std::vector<glm::mat4> palette);
    void update(float seconds);
    [[nodiscard]] std::size_t ragdollCount() const;
    // Dense, unordered list of stable instance IDs, including externally driven
    // corpses. Borrowed view invalidated by corpse creation/removal or rebuild.
    const std::vector<std::size_t>& ragdollIds() const { return ragdollIds_; }
    // Owning APIs notify automatically. After direct instance edits, notify the
    // ID (default also refreshes bounds), or invalidate after bulk replacement.
    void markRenderChanged(std::size_t id, bool bounds = true);
    void invalidateRenderData() { renderUpdates_.invalidate(); }
    const ZombieRenderUpdates& renderUpdates() const { return renderUpdates_; }

private:
    struct Physics;
    std::unique_ptr<Physics> physics_;
    ZombieRenderUpdates renderUpdates_;
    void registerRagdoll(std::size_t zombie);
    void unregisterRagdoll(std::size_t zombie);
    void rebuildRagdollIndex();
    std::vector<std::size_t> ragdollIds_;
    std::unordered_map<std::size_t, std::size_t> ragdollSlots_;
    void ensureSpatialIndex();
    void updateSpatialMembership(std::size_t id);
    std::size_t findShotTarget(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                               std::vector<glm::mat4>& selectedPose);
    ZombieSpatialIndex spatialIndex_;
    ZombieCrowd crowd_;
    std::vector<std::size_t> behaviorOrder_;
    std::vector<ZombieHitGeometry> hitGeometry_;
    std::vector<ZombieHitBounds> shotBounds_;
    double shotPadding_ = 0;
    double shotCoordinateScale_ = 0;
    ZombieShootingStats shootingStats_;
    std::vector<std::size_t> spatialCandidates_;
    bool staggerPerception_ = true;
    ZombieBehaviorStats behaviorStats_;
};

bool hasZombieModels(const std::filesystem::path& source);

// Source is one animated glTF/GLB or a directory of individual characters.
// Character height is normalized to 1.8 m; feet use the rendered town surface.
ZombieLayer loadZombieLayer(const std::filesystem::path& source,
                           const CollisionWorld& world, const Model& scene,
                           glm::vec2 center, std::size_t count = 1000,
                           float radius = 100.0f,
                           const std::vector<glm::vec2>& boundary = {});

} // namespace viewer
