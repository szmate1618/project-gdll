#pragma once

#include "zombie_movement.hpp"
#include <glm/vec2.hpp>
#include <cstdint>

namespace viewer {
class ZombieNavigation;

// Short accepted straight segment. Authoritative feet advance smoothly each
// frame; no separate render position or future position enters gameplay indexes.
struct TravelerPlan {
    glm::vec3 target{0}, lastFeet{0};
    std::uint64_t worldId = 0;
    glm::vec2 velocity{0};
    float remaining = 0, fallbackSeconds = 0, startHeight = 0;
    bool distant = false, phased = false;
};
struct TravelerMovement {
    ZombieMovement movement;
    glm::vec2 velocity{0};
};

TravelerMovement moveScenarioTraveler(std::size_t id, glm::vec3 start, float seconds,
    const CollisionWorld& world, ZombieNavigation& navigation, ZombieCrowd& crowd,
    WalkingGroundCache& ground, ContactCandidates& contacts, TravelerPlan& plan,
    bool distant);
} // namespace viewer
