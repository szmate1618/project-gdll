#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace viewer {
class ZombieLayer;
class CollisionWorld;

constexpr double sleepGuardScanSeconds = 5;
constexpr std::size_t sleepGuardScanLimit = 20;
float sleepGuardSightRange(double hour);
// Compass bearings clockwise from north, in 22.5-degree sectors. X east/Z south.
std::uint8_t sleepGuardBearing(glm::vec3 offset);
glm::vec2 sleepGuardLookAngles(glm::vec3 eyes, glm::vec3 target); // Yaw/pitch degrees.

struct SleepGuardSighting {
    std::size_t zombie = 0;
    glm::vec3 head{0};
    std::uint8_t bearing = 0;
};

// One per guard, owned by the authority. Uses the living zombie index and casts
// at most one head ray for each of the nearest twenty actors per observation.
class SleepGuardWatch {
public:
    std::optional<SleepGuardSighting> update(ZombieLayer& zombies, const CollisionWorld& world,
        glm::vec3 feet, glm::vec3 eyes, double seconds, double hour);
    void reset() { nextScanAt_ = -1; visibilityChecks_ = 0; }
    std::size_t visibilityChecks() const { return visibilityChecks_; }
private:
    double nextScanAt_ = -1;
    std::size_t visibilityChecks_ = 0;
    std::vector<std::size_t> candidates_;
};
} // namespace viewer
