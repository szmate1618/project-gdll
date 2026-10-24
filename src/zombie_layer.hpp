#pragma once

#include "animated_model.hpp"
#include "collision_world.hpp"
#include "zombie_behavior.hpp"
#include "zombie_spatial_index.hpp"
#include "zombie_crowd.hpp"
#include "zombie_ragdoll.hpp"
#include "zombie_hit.hpp"
#include "zombie_damage.hpp"
#include "zombie_dismemberment.hpp"
#include "zombie_render_updates.hpp"
#include "zombie_wounds.hpp"
#include "blood_mist.hpp"
#include "blood_particles.hpp"

#include <memory>
#include <optional>
#include <unordered_map>

namespace viewer {

class PhysicsWorld;
class ZombieNavigation;
class PhysicsScene;

struct ZombieInstance {
    std::size_t asset = 0;
    glm::mat4 transform{1};
    // Live placement until death; then frozen as the ragdoll handoff basis.
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
    // Actual world-space feet displacement per second after scene/crowd collision.
    // Carried into every physics body at death, independently of clip motion.
    glm::vec3 locomotionVelocity{0};
    // Presentation notifications only. Counters survive behavior-pool eviction;
    // timestamps let audio discard old reactions when a source enters range.
    std::uint64_t heardNoiseRevision = 0, chaseRevision = 0;
    double heardNoiseSeconds = -1, chaseSeconds = -1;
    // Presentation hysteresis for intermittent crowd/door contact. Neither
    // timer changes physical movement or evaluates a per-character skeleton.
    float blockedAnimationSeconds = 0, animationRestartSeconds = 0;
    bool ragdoll = false;
    bool removed = false; // Tombstone: source IDs remain stable after corpse removal.
    std::size_t ragdollHandle = 0;
    bool physicsRagdoll = false;
    int severedJoint = -1; // Index in the original asset's joint manifest; persists on corpses.
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
    std::size_t attackChecks = 0, attackRays = 0;
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
    std::vector<ZombieDismembermentAsset> dismembermentAssets;
    std::vector<ZombieCapsuleAsset> capsuleAssets;
    // Per original asset, ordered LOD1..3; originals own gameplay/rig identity.
    std::vector<std::vector<AnimatedModel>> lodAssets;
    std::vector<ZombieInstance> instances;
    ZombieBehaviorPool behaviors;
    glm::vec3 boundsMin{0}, boundsMax{0};

    // Returns true when the nearest living zombie takes damage. Only zero HP
    // activates its ragdoll; each call represents one bullet or shotgun pellet.
    // animationSeconds is the time of the last displayed frame (before phase).
    bool shoot(glm::vec3 origin, glm::vec3 direction, double animationSeconds = 0.0,
               ShotCaliber caliber = ShotCaliber::low);
    bool shootWithOcclusion(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                            const CollisionWorld& world, ShotCaliber caliber = ShotCaliber::low);
    const ZombieDamage& damage() const { return damage_; }
    const ZombieWounds& wounds() const { return wounds_; }
    const BloodMist& bloodMist() const { return bloodMist_; }
    const BloodParticles& bloodParticles() const { return bloodParticles_; }
    void updateBloodEffects(double seconds); // Resolve sparse ragdoll emitters, then retire droplets.
    void setReplicatedBloodMist(const std::vector<BloodMistBurst>& bursts, double snapshotSeconds) {
        bloodMist_.replace(bursts,snapshotSeconds);
    }
    // Sparse wounded actors only. Call before advancing the shared physics world.
    void prepareWoundPhysics(float seconds, double animationSeconds, PhysicsScene* scene = nullptr);
    std::vector<glm::mat4> woundContents() const;
    void setWoundMask(std::size_t zombie, const std::vector<std::uint8_t>& mask);
    void setReplicatedWoundContents(std::vector<glm::mat4> transforms);
    std::size_t explode(glm::vec3 origin, float radius, double animationSeconds, const CollisionWorld& world);
    const ZombieShootingStats& shootingStats() const { return shootingStats_; }
    // Silent noise at a world position, with a source level in dB at one meter.
    // Only idle, living zombies already in the behavior pool can hear it.
    void makeNoise(glm::vec3 position, float decibels);
    // Exact 3D feet query using the living-body index, independent of camera
    // visibility and behavior activation. Reuses the caller's output storage.
    void queryLivingNear(glm::vec3 center, float radius, std::vector<std::size_t>& ids);
    // Disable only for comparison/tests of the original every-step perception.
    void setPerceptionStaggering(bool enabled);
    // Disabling clears active behaviors and bypasses activation/sight/hearing
    // scans. Authored scenario routes, shooting and physics remain independent.
    void setPerceptionEnabled(bool enabled);
    const ZombieBehaviorStats& behaviorStats() const { return behaviorStats_; }
    const ZombieCrowdStats& crowdStats() const { return crowd_.stats(); }
    // Only cold scenario loading enables navigation. Ordinary zombies and
    // replicated clients never load a mesh or request routes.
    void enableNavigation(const std::filesystem::path& navmeshPath);
    void enableNavigation(std::unique_ptr<ZombieNavigation> navigation);
    const ZombieNavigation* navigation() const { return navigation_.get(); }
    // Cold scenario setup: preplan one initial destination per living instance.
    // Travel runs without nearby players. Perception can permanently hand an
    // actor to original behavior movement, releasing its route without a search.
    void setScenarioDestinations(const std::vector<glm::vec3>& destinations);
    const glm::vec3* scenarioDestination(std::size_t id) const;
    bool scenarioFinished(std::size_t id) const;
    // Rebuild after external edits to instance feet or replacing a population
    // with the same size, or edits to asset rigs/clips or instance normalization.
    // Rebuilds hit geometry/corpse membership and resets enabled routes. Call after
    // initial fixture/bulk edits to ragdoll/removed flags. Ordinary locomotion
    // and the corpse owning APIs maintain these indices incrementally.
    void rebuildSpatialIndex();
    // Perception/state update, followed by scene/crowd-constrained locomotion.
    // The app advances SimulationClock before calling this once per step.
    void updateBehavior(float seconds, const ZombiePlayer& player, const CollisionWorld& world,
                        double animationSeconds = 0.0);
    // Observes all players and advances each zombie exactly once.
    void updateBehaviorForPlayers(float seconds, const std::vector<ZombiePlayer>& players,
                                  const CollisionWorld& world, double animationSeconds);
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
    void emitBloodParticles(const ZombieShotHit& hit, glm::vec3 point, glm::vec3 direction,
                            double seconds, const CollisionWorld* world);
    void activateRagdoll(std::size_t zombie, std::vector<glm::mat4> pose, glm::vec3 velocity, double animationSeconds,
                         int severedJoint = -1, ShotCaliber caliber = ShotCaliber::low);
    bool shootImpl(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                   const CollisionWorld* occlusion, ShotCaliber caliber);
    ZombieDamage damage_;
    ZombieWounds wounds_;
    BloodMist bloodMist_;
    BloodParticles bloodParticles_;
    std::vector<ZombieWoundRegions> woundGeometry_;
    std::optional<std::vector<glm::mat4>> replicatedWoundContents_;
    void updateBehaviorImpl(float seconds, const ZombiePlayer& player, const CollisionWorld& world,
                            double animationSeconds, const std::vector<ZombiePlayer>* players);
    friend class ZombieDoorCollision; // Reuse the living-body spatial index for door-local physics proxies.
    struct Physics;
    std::unique_ptr<Physics> physics_;
    std::unique_ptr<ZombieNavigation> navigation_;
    struct ScenarioRoute {
        glm::vec3 destination{0};
        bool finished = false, overridden = false;
        WalkingGroundCache groundCache;
        ContactCandidates contactCache;
    };
    std::vector<ScenarioRoute> scenarioRoutes_;
    void updateScenarioRoutes(float seconds, const CollisionWorld& world, bool advanceLooking = false);
    void abandonScenarioRoute(std::size_t id);
    ZombieRenderUpdates renderUpdates_;
    void registerRagdoll(std::size_t zombie);
    void unregisterRagdoll(std::size_t zombie);
    void rebuildRagdollIndex();
    std::vector<std::size_t> ragdollIds_;
    std::unordered_map<std::size_t, std::size_t> ragdollSlots_;
    void ensureSpatialIndex();
    void updateSpatialMembership(std::size_t id);
    std::optional<ZombieShotHit> findShotTarget(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                                              std::vector<glm::mat4>& selectedPose);
    ZombieSpatialIndex spatialIndex_;
    ZombieCrowd crowd_;
    std::vector<std::size_t> behaviorOrder_;
    std::vector<ZombieHitGeometry> hitGeometry_;
    std::vector<ZombieHitBounds> shotBounds_;
    double shotPadding_ = 0;
    ZombieShootingStats shootingStats_;
    std::vector<std::size_t> spatialCandidates_;
    bool staggerPerception_ = true;
    bool perceptionEnabled_ = true;
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

// Explicit safe placements, used by scenario generation before asset loading.
ZombieLayer loadZombieLayer(const std::filesystem::path& source,
                           const std::vector<glm::mat4>& placements);

} // namespace viewer
