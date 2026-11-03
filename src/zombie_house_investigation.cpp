#include "zombie_house_investigation.hpp"
#include "building_footprints.hpp"
#include "floorplan_layer.hpp"
#include "zombie_layer.hpp"
#include "zombie_chase.hpp"
#include "zombie_movement.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
void ZombieHouseInvestigation::setFloorplans(const FloorplanLayer* floorplans) {
    if (floorplans != floorplans_) { routes_.clear(); playerHouses_.clear(); }
    floorplans_ = floorplans;
}
void ZombieHouseInvestigation::updatePlayers(const ZombiePlayer& player, const std::vector<ZombiePlayer>* players) {
    playerHouses_.clear();
    if (players) { for (const auto& candidate : *players) if (candidate.house) playerHouses_.emplace_back(*candidate.house); }
    else if (player.house) playerHouses_.emplace_back(*player.house);
    std::sort(playerHouses_.begin(), playerHouses_.end());
    playerHouses_.erase(std::unique(playerHouses_.begin(), playerHouses_.end()), playerHouses_.end());
}
bool ZombieHouseInvestigation::prepare(ZombieBehavior& behavior, glm::vec3 feet) {
    if (behavior.state != ZombieState::investigating && behavior.state != ZombieState::houseInvestigating) {
        erase(behavior.zombie); return false;
    }
    auto found = routes_.find(behavior.zombie);
    if (found == routes_.end() && behavior.state == ZombieState::houseInvestigating) behavior.state = ZombieState::investigating;
    if (found != routes_.end()) {
        const auto* graph = floorplans_ ? floorplans_->navigationGraph(found->second.house) : nullptr;
        if (graph && graph->generation() == found->second.generation && behavior.target == found->second.target) {
            behavior.state = ZombieState::houseInvestigating; return true;
        }
        if (graph && floorplans_->footprints().contains(found->second.house, behavior.target)) {
            if (graph->generation() != found->second.generation || graph->roomAt(behavior.target) != found->second.path.room) {
                found->second.generation = graph->generation();
                found->second.path = graph->route(feet, behavior.target); found->second.next = 0;
            }
            found->second.target = behavior.target;
            behavior.state = ZombieState::houseInvestigating; return true;
        }
        routes_.erase(found); behavior.state = ZombieState::investigating;
    }
    if (!floorplans_) return false;
    for (const auto& house : playerHouses_) {
        if (!floorplans_->footprints().contains(house, feet) || !floorplans_->footprints().contains(house, behavior.target)) continue;
        const auto* graph = floorplans_->navigationGraph(house);
        if (!graph) continue;
        routes_.emplace(behavior.zombie, Route{house, graph->generation(), behavior.target, graph->route(feet, behavior.target), 0});
        behavior.state = ZombieState::houseInvestigating; return true;
    }
    return false;
}
bool ZombieHouseInvestigation::move(ZombieBehavior& behavior, ZombieInstance& instance, float seconds,
                                    const CollisionWorld& world, ZombieCrowd& crowd) {
    const auto found = routes_.find(behavior.zombie);
    if (found == routes_.end()) { stopZombieLocomotion(instance); return false; }
    auto& route = found->second;
    if (route.path.points.empty()) { stopZombieLocomotion(instance); return false; } // No valid graph path: never cut through walls.
    const auto reached = [&](glm::vec3 target) {
        const auto feet = glm::vec3(instance.restRoot[3]);
        return glm::length(glm::vec2(feet.x - target.x, feet.z - target.z)) < .07f && std::abs(feet.y - target.y) < .25f;
    };
    while (route.next < route.path.points.size()) {
        auto next = route.next;
        const bool door = route.path.points[next].door && next + 1 < route.path.points.size();
        if (door) ++next; // Cross to the next room's centerline anchor, not just the panel plane.
        const auto target = route.path.points[next].point;
        if (reached(target)) { route.next = next + 1; continue; }
        const auto start = glm::vec3(instance.restRoot[3]);
        const auto queued = door ? crowd.doorwayDestination(behavior.zombie, target, world) : std::nullopt;
        const auto destination = queued.value_or(target);
        const glm::vec2 offset(destination.x - start.x, destination.z - start.z);
        const float distance = glm::length(offset);
        if (distance < 1e-5f) { stopZombieLocomotion(instance); return false; }
        constexpr float speed = 1.2f;
        const auto velocity = offset / distance * std::min(speed, distance / seconds);
        const auto movement = moveZombieBody(start, start + glm::vec3(velocity.x, 0, velocity.y) * seconds, world, &crowd, behavior.zombie);
        applyZombieLocomotion(instance, velocity, movement, speed, seconds);
        if (reached(target)) route.next = next + 1;
        return route.next == route.path.points.size();
    }
    stopZombieLocomotion(instance); return true;
}
} // namespace viewer
