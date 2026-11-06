#include "zombie_update_profile.hpp"
#include "zombie_layer.hpp"
#include "zombie_chase.hpp"
#include "zombie_movement.hpp"
#include "zombie_navigation.hpp"
#include "simulation_clock.hpp"
#include "sleep_plan.hpp"
#include <limits>
#include <utility>
#include <cmath>

namespace viewer {
namespace {
constexpr float walkSpeed = 1.2f;

bool followScenarioRoute(ZombieInstance& instance, glm::vec3 destination, float seconds,
                         const CollisionWorld& world, ZombieCrowd& crowd, std::size_t id,
                         ZombieNavigation& navigation, WalkingGroundCache& groundCache,ContactCandidates& contacts,TravelerPlan& travel,
                         bool distant) {
    const auto start = glm::vec3(instance.restRoot[3]);
    if (glm::length(glm::vec2(destination.x-start.x,destination.z-start.z)) <= 1e-4f) {
        return true;
    }
    const auto step=moveScenarioTraveler(id,start,seconds,world,navigation,crowd,groundCache,contacts,travel,distant);
    const auto& movement=step.movement;
    navigation.recordMovement(id,start,movement.feet,seconds);
    applyZombieLocomotion(instance,step.velocity,movement,walkSpeed,seconds);
    return glm::length(glm::vec2(destination.x - movement.feet.x, destination.z - movement.feet.z)) <= 1e-4f;
}
} // namespace

void ZombieLayer::abandonScenarioRoute(std::size_t id) {
    const auto found = scenarioRoutes_.find(id);
    if (found == scenarioRoutes_.end() || found->second.overridden) return;
    found->second.overridden = true;
    found->second.travel = {};
    if (navigation_) navigation_->reset(id);
}
void ZombieLayer::setSleepTravel(bool enabled, const CollisionWorld& world) {
    if (sleepTravel_ == enabled) return;
    sleepTravel_ = enabled; sleepTravelSeconds_ = 0; sleepTravelThreat_ = false;
    for (auto& [id, route] : scenarioRoutes_) {
        route.travel = {}; route.groundCache = {}; route.contactCache = {};
        if (enabled || route.overridden || instances[id].removed || instances[id].ragdoll) continue;
        auto& instance = instances[id];
        auto feet = glm::vec3(instance.restRoot[3]);
        if (const auto ground = world.groundAt(feet.x, feet.z, std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), true)) {
            feet.y = ground->height;
            applyZombieLocomotion(instance, {}, {feet}, 1.2f, 0);
            markRenderChanged(id, true); updateSpatialMembership(id);
        }
    }
}
bool ZombieLayer::takeSleepTravelThreat() { return std::exchange(sleepTravelThreat_, false); }

void ZombieLayer::updateScenarioRoutes(float seconds, const CollisionWorld& world,const ZombiePlayer& player,
                                      const std::vector<ZombiePlayer>* players,bool advanceLooking) {
    zombie_profile::Timer timer(zombie_profile::Stage::scenarioRoutes);
    zombie_profile::PhaseTimer phase(&zombie_profile::Profile::scenarioSeconds);
    if (scenarioRoutes_.empty()) return;
    if (scenarioPopulation_ != instances.size()) {
        scenarioRoutes_.clear(); // Bulk population replacement invalidates scenario IDs.
        scenarioPopulation_ = 0;
        if (navigation_) navigation_->resetPopulation(instances.size());
        return;
    }
    if (sleepTravel_) {
        sleepTravelSeconds_ += seconds;
        if (sleepTravelSeconds_ < .5f) return;
        const float elapsed = std::exchange(sleepTravelSeconds_, 0.f);
        std::vector<glm::vec3> sleepers;
        if (players) { for (const auto& observer : *players) sleepers.push_back(observer.feet); }
        else sleepers.push_back(player.feet);
        for (auto& [id, route] : scenarioRoutes_) {
            auto& instance = instances[id];
            if (route.finished || route.overridden || instance.removed || instance.ragdoll) continue;
            const auto start = glm::vec3(instance.restRoot[3]);
            const auto step = navigation_->advanceSleep(id, start, elapsed, sleepers, sleepWakeDistanceMeters);
            if (step.nearSleeper) { sleepTravelThreat_ = true; continue; }
            const glm::vec3 feet{step.position.x, start.y, step.position.y};
            applyZombieLocomotion(instance, (step.position - glm::vec2(start.x,start.z)) / elapsed,
                {feet,step.travel}, walkSpeed, elapsed);
            if (step.arrived) { route.finished = true; navigation_->reset(id); stopZombieLocomotion(instance); }
            markRenderChanged(id, true); updateSpatialMembership(id);
        }
        return;
    }
    for (auto& [id, route] : scenarioRoutes_) {
        auto& instance = instances[id];
        if (route.finished || route.overridden) continue;
        const auto beforeTransform = instance.transform;
        const auto beforeRoot = instance.restRoot;
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
            // Hysteresis leaves a wide full-rate buffer around perception range.
            // Check every player, including observers unrelated to this actor's
            // selected behavior target. No players permits distant travel.
            const float range=route.travel.distant?160.f:200.f;
            const auto near=[&](const ZombiePlayer& observer) {
                const glm::vec2 offset(observer.feet.x-instance.restRoot[3].x,observer.feet.z-instance.restRoot[3].z);
                return !std::isfinite(offset.x) || !std::isfinite(offset.y) || glm::dot(offset,offset)<=range*range;
            };
            bool distant=true;
            if (players) { for (const auto& observer:*players) if (near(observer)) { distant=false;break; } }
            else distant=!near(player);
            if (looking) route.travel={};
            if (!looking && followScenarioRoute(instance, route.destination, seconds, world, crowd_, id, *navigation_, route.groundCache,route.contactCache,route.travel,distant && distantScenarioTravel_)) {
                route.finished = true;
                if (navigation_) navigation_->reset(id);
                stopZombieLocomotion(instance);
            }
        }
        const bool moved = beforeTransform != instance.transform;
        if (moved || wasWalking != instance.walking || wasRunning != instance.running || beforeSeconds != instance.walkSeconds)
            markRenderChanged(id, moved);
        if(moved || beforeRoot!=instance.restRoot || instance.removed || instance.ragdoll || instance.asset>=assets.size())
            updateSpatialMembership(id);
    }
}

} // namespace viewer
