#pragma once

#include "house_navigation.hpp"
#include "zombie_behavior.hpp"
#include <optional>
#include <unordered_map>

namespace viewer {
class FloorplanLayer;
class CollisionWorld;
class ZombieCrowd;
struct ZombieInstance;

// Sparse active routes. Graphs are looked up by building/generation on each
// update, so streaming never leaves a borrowed graph pointer in an actor.
class ZombieHouseInvestigation {
public:
    void setFloorplans(const FloorplanLayer* floorplans);
    void updatePlayers(const ZombiePlayer& player, const std::vector<ZombiePlayer>* players);
    bool prepare(ZombieBehavior& behavior, glm::vec3 feet);
    // Optional output receives a door contact from this movement only.
    bool move(ZombieBehavior& behavior, ZombieInstance& instance, float seconds,
              const CollisionWorld& world, ZombieCrowd& crowd,
              std::optional<glm::vec3>* doorContact = nullptr);
    void erase(std::size_t id) { routes_.erase(id); }
    void clear() { routes_.clear(); }
private:
    struct Route {
        std::string house;
        std::uint64_t generation;
        glm::vec3 target;
        HouseNavigationPath path;
        std::size_t next = 0;
    };
    const FloorplanLayer* floorplans_ = nullptr;
    std::vector<std::string> playerHouses_;
    std::unordered_map<std::size_t, Route> routes_;
};

} // namespace viewer
