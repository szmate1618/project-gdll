#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace viewer {
class CollisionWorld;

struct DogWandering {
    glm::vec2 direction{0, 1}; // Original trip direction in world X/Z.
    glm::vec3 previous{0}; // Return waypoint: most recent leg's start, world feet meters.
    int remainingLegs = 0;
    bool hasTarget = false, returning = false;
    float stalledSeconds = 0;
    double nextPartCheck = 0;
    std::vector<glm::vec3> waypoints; // Home first, followed by visited leg endpoints.
};

// Unit X/Z direction D and distance in meters (the caller selects 100-300).
// Test D, +30, -30, +45, -45 degrees, always relative to the original D.
// Returns a supported endpoint with clear eye-height sight, not a pathfinding
// guarantee. Normal capsule movement still checks every physical step.
std::optional<glm::vec3> dogWanderTarget(glm::vec3 feet, glm::vec2 direction,
                                      float distance, const CollisionWorld& world);
} // namespace viewer
