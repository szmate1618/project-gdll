#pragma once

#include "house_floorplan.hpp"
#include "day_night_clock.hpp"
#include <functional>
#include <memory>

namespace viewer {
class CollisionWorld;
class DoorLayer;
class FloorplanLayer;
struct ZombieLayer;
inline constexpr std::size_t maxInteriorZombies = 6; // Ordinary encounters; palace has a separate bound.
inline constexpr std::size_t palaceInteriorZombies = 200;
inline constexpr std::size_t interiorZombieReserve = 10000;
enum class InteriorZombiePosition { BehindDoor, BesideDoor, Corner, CenterGroup, RoomPopulation };
struct InteriorZombiePlacement {
    glm::mat4 transform{1}; // Feet, runtime X-east/Y-up/Z-south metres; +Z faces the doorway.
    std::size_t room = 0;
    InteriorZombiePosition position = InteriorZombiePosition::Corner;
};
bool hasInteriorZombieEncounter(const std::string& building);
std::vector<InteriorZombiePlacement> placeInteriorZombies(const HouseFloorplan& plan,
    const DoorLayer& doors, const CollisionWorld& collision,
    const std::function<bool(glm::vec3)>& available = {}); // Optional observer/living-actor clearance.

// Authoritative session state. Stable dormant IDs are reserved before loading
// the zombie assets/renderer. Geometry eviction cannot reroll or respawn kills.
class InteriorZombies {
public:
    // One shared tail of the fixed population, never six slots per building.
    explicit InteriorZombies(std::size_t firstZombie, std::size_t reserved = interiorZombieReserve);
    ~InteriorZombies();
    std::size_t reserveCount() const;
    void update(const FloorplanLayer& floors, ZombieLayer& zombies, const CollisionWorld& collision,
                const std::vector<glm::vec3>& observers, const DayNightCycle& dayNight = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
