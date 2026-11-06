#pragma once
#include "animated_model.hpp"
#include "collision_world.hpp"
#include "zombie_behavior.hpp" // Shared player observations and perception cadence only.
#include "zombie_spatial_index.hpp"
#include "noise.hpp"
#include "dog_wandering.hpp"
#include "dog_fetch.hpp"
#include "doorway_guidance.hpp"
#include "dog_hunting.hpp"
#include "scent_grid.hpp"
#include "zombie_navigation.hpp"
#include <filesystem>
#include <optional>
#include <random>

namespace viewer {
constexpr std::size_t maxDogs = 1000;
constexpr float dogRadius = .25f, dogHeight = .9f;
// Meters per animation second. Actual travel / reference speed keeps playback
// proportional when gameplay movement speeds change.
constexpr float dogWalkAnimationSpeed = 1.2f, dogRunAnimationSpeed = 3.f;
class ZombieDetachedParts;
class PhysicsScene;
class DoorLayer;
enum class DogState { idle, looking, investigating, chasing, attacking, wandering, fetching, hunting };
struct DogBehavior {
    DogState state = DogState::idle;
    glm::vec3 target{0};
    glm::vec2 lookDirection{0, 1};
    ZombiePerceptionClock perception;
    double idleSince = 0, attackStarted = -1, readyAt = 0;
    std::uint32_t targetPlayer = 0;
    int sight = 0; // 0 blocked, 1 partial, 2 full.
    bool active = false, suspended = false, attackHit = false;
    DogWandering wander;
    std::optional<DogFetching> fetch;
    std::optional<DogHunting> hunt;
};
struct DogInstance {
    std::size_t asset = 0;
    glm::mat4 root{1}, transform{1}; // World feet/yaw, followed by asset normalization.
    float phase = 0, health = 100;
    float displayedAnimationFrame = -1; // Renderer snapshot only; never simulation or wire state.
    double walkSeconds = 0, attackSeconds = 0;
    bool walking = false, running = false, attacking = false, dead = false, removed = false;
    std::optional<glm::vec3> home; // Original authoritative spawn; never reset by AI/index rebuilds.
    double nextHuntSeconds = 0;
};
struct DogAttackHit { std::size_t dog; std::uint32_t player; float damage; };
struct DogShotTrace { std::size_t dog; float distance; };
struct DogDetail { std::size_t level = 0; float animationHz = 0; };
DogDetail dogCameraDetail(float distance);
const BakedAnimationClip* dogAnimationClip(const AnimatedModel& model, const DogInstance& dog);
float dogAnimationFrame(const AnimatedModel& model, const DogInstance& dog, double seconds, float hz = 0);
VisibilityBounds dogWorldBounds(const VisibilityBounds& bounds, const glm::mat4& transform);

// Independent stable IDs, behavior storage, collision index and animation clocks.
// Shared utilities own no dog or humanoid population state.
class DogLayer {
public:
    std::vector<AnimatedModel> assets;
    std::vector<std::vector<AnimatedModel>> lodAssets;
    std::vector<VisibilityBounds> assetBounds;
    std::vector<DogInstance> instances;
    std::vector<DogBehavior> behaviors;
    void rebuildSpatialIndex();
    void updateBehavior(float dt, const std::vector<ZombiePlayer>& players, const CollisionWorld& world, double now,
                        ZombieDetachedParts* parts = nullptr);
    void prepareFetchPhysics(PhysicsScene& scene, DoorLayer* doors = nullptr) const;
    void makeNoise(glm::vec3 position, float decibels);
    void makeNoise(const NoiseEvent& noise);
    const std::vector<DogAttackHit>& attackHits() const { return hits_; }
    void releaseAnimationVertices();
    std::optional<DogShotTrace> traceShot(glm::vec3 origin, glm::vec3 direction,
        const CollisionWorld& world, double seconds, const std::vector<DogInstance>* displayed = nullptr) const;
    bool damage(std::size_t dog, float amount);
    void updateScent(double now, const std::vector<ScentEmitter>& emitters) { scent_.update(now,emitters); }
    const ScentGrid& scent() const { return scent_; }
    void enableHunting(std::shared_ptr<const navmesh_path::Mesh> mesh, const BuildingFootprints* footprints = nullptr);
    const ZombieNavigation* huntingNavigation() const { return huntingNavigation_.get(); }
private:
    ScentGrid scent_;
    std::unique_ptr<ZombieNavigation> huntingNavigation_; // Dog-only IDs, paths, search and progress.
    const BuildingFootprints* huntingFootprints_ = nullptr; // Borrows the session's immutable metadata.
    unsigned huntAttemptsRemaining_ = 0;
    bool tryStartHunt(std::size_t id, const CollisionWorld& world, double now);
    bool prepareHuntStep(std::size_t id, const CollisionWorld& world, double now);
    bool beginHuntReturn(std::size_t id, const CollisionWorld& world, double now);
    void cancelHunt(std::size_t id);
    void finishHunt(std::size_t id, double now);
    std::optional<glm::vec3> huntingCellTarget(ScentGrid::Cell cell, glm::vec3 from, const CollisionWorld& world) const;
    bool validHuntingTarget(glm::vec3 target, const CollisionWorld& world) const;
    ZombieSpatialIndex spatial_; // A distinct grid; never contains humanoid IDs.
    DoorwayGuidance doorways_{dogRadius, dogHeight}; // Dog-only requests and owners.
    std::vector<std::size_t> candidates_;
    std::vector<DogAttackHit> hits_;
    std::mt19937 wanderRandom_{std::random_device{}()};
    std::optional<double> nextWanderSelection_;
    void selectWanderers(double now);
    bool prepareWanderStep(std::size_t id, const CollisionWorld& world, double now);
    void findFetchPart(std::size_t id, ZombieDetachedParts& parts, const CollisionWorld& world, double now);
    bool prepareFetchStep(std::size_t id, ZombieDetachedParts* parts, const CollisionWorld& world, double now);
    glm::vec3 move(std::size_t id, glm::vec3 desired, const CollisionWorld& world);
    glm::vec2 steeringDirection(std::size_t id, glm::vec2 targetOffset, const CollisionWorld& world);
};
std::vector<std::filesystem::path> dogAssetPaths(const std::filesystem::path& directory);
DogLayer loadDogLayer(const std::filesystem::path& directory,
    const std::vector<glm::mat4>& placements, bool replica = false);
DogLayer loadDogLayer(const std::filesystem::path& directory, const CollisionWorld& world,
    const Model& scene, glm::vec2 center, std::size_t count, float radius = 100);
} // namespace viewer
