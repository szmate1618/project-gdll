#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <optional>

namespace viewer {
class CollisionWorld;
class ZombieCrowd;
class WalkingGroundCache;
class ContactCandidates;

struct ZombieMovement {
    glm::vec3 feet;
    float travel = 0; // Actual horizontal path length in meters, for animation.
    // First world-space door contact encountered while resolving this move,
    // including moves that subsequently slide or step past the panel.
    std::optional<glm::vec3> doorContact = std::nullopt;
};

// Local spreading needs 3 m of extra horizontal scene clearance. Query nearby
// contacts, ignoring floors/ceilings; no visibility tests or crowd mutation.
bool zombieHasPassingClearance(glm::vec3 feet, const CollisionWorld& world);

// Terrain-following capsule motion against the shared scene and living crowd.
// Queries dynamic props at their current pose without applying impulses. Short
// segments prevent crossing thin barriers; unresolved contacts retain safe feet.
// Does not mutate the crowd index or publish transforms.
// Building walls have 5 cm extra clearance; other obstacles use the body radius.
// A supplied crowd counter shares the neighbor budget across several requests
// belonging to one movement (for example, collision-clipped attack segments).
ZombieMovement moveZombieBody(glm::vec3 start, glm::vec3 desired,
                              const CollisionWorld& world,
                              ZombieCrowd* crowd = nullptr, std::size_t id = 0,
                              WalkingGroundCache* groundCache = nullptr,
                              ContactCandidates* contactCache = nullptr,
                              std::size_t* sharedCrowdChecks = nullptr);
} // namespace viewer
