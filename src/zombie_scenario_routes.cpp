#include "zombie_layer.hpp"
#include "zombie_chase.hpp"
#include "zombie_movement.hpp"
#include "zombie_navigation.hpp"
#include "simulation_clock.hpp"

namespace viewer {
namespace {
constexpr float walkSpeed = 1.2f;

bool followScenarioRoute(ZombieInstance& instance, glm::vec3 destination, float seconds,
                         const CollisionWorld& world, ZombieCrowd& crowd, std::size_t id,
                         ZombieNavigation& navigation, WalkingGroundCache& groundCache,ContactCandidates& contacts) {
    const auto start = glm::vec3(instance.restRoot[3]);
    if (glm::length(glm::vec2(destination.x - start.x, destination.z - start.z)) <= 1e-4f) return true;
    const auto velocity = navigation.follow(id, start, walkSpeed, seconds, world);
    auto movement = moveZombieBody(start,
        start + glm::vec3(velocity.x * seconds, 0, velocity.y * seconds), world, &crowd, id, &groundCache,&contacts);
    // Collision/crowd sliding can change the proposed direction. Validate the
    // final physical step before publishing its pose or animation progress.
    if (!navigation.movementClear(id, start, movement.feet)) movement = {start};
    navigation.recordMovement(id, start, movement.feet, seconds);
    applyZombieLocomotion(instance, velocity, movement, walkSpeed, seconds);
    return glm::length(glm::vec2(destination.x - movement.feet.x, destination.z - movement.feet.z)) <= 1e-4f;
}
} // namespace

void ZombieLayer::abandonScenarioRoute(std::size_t id) {
    if (id >= scenarioRoutes_.size() || scenarioRoutes_[id].overridden) return;
    scenarioRoutes_[id].overridden = true;
    if (navigation_) navigation_->reset(id);
}

void ZombieLayer::updateScenarioRoutes(float seconds, const CollisionWorld& world, bool advanceLooking) {
    if (scenarioRoutes_.empty()) return;
    if (scenarioRoutes_.size() != instances.size()) {
        scenarioRoutes_.clear(); // Bulk population replacement invalidates scenario IDs.
        if (navigation_) navigation_->resetPopulation(instances.size());
        return;
    }
    for (std::size_t id = 0; id < scenarioRoutes_.size(); ++id) {
        auto& route = scenarioRoutes_[id];
        auto& instance = instances[id];
        if (route.finished || route.overridden) continue;
        const auto beforeTransform = instance.transform;
        const auto beforeSeconds = instance.walkSeconds;
        const bool wasWalking = instance.walking, wasRunning = instance.running;
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size()) {
            route.finished = true;
            if (navigation_) navigation_->reset(id);
            stopZombieLocomotion(instance);
        } else {
            auto* behavior = behaviors.find(id);
            if (behavior && behavior->state == ZombieState::looking && advanceLooking &&
                turnZombieToward(instance, behavior->lookDirection, seconds)) {
                behavior->state = ZombieState::idle;
                behavior->idleSinceSeconds = SimulationClock::instance().seconds();
            }
            const bool looking = behavior && behavior->state == ZombieState::looking;
            if (!looking && followScenarioRoute(instance, route.destination, seconds, world, crowd_, id, *navigation_, route.groundCache,route.contactCache)) {
                route.finished = true;
                if (navigation_) navigation_->reset(id);
                stopZombieLocomotion(instance);
            }
        }
        const bool moved = beforeTransform != instance.transform;
        if (moved || wasWalking != instance.walking || wasRunning != instance.running || beforeSeconds != instance.walkSeconds)
            markRenderChanged(id, moved);
        updateSpatialMembership(id);
    }
}

} // namespace viewer
