#pragma once
#include <glm/vec3.hpp>
#include <vector>
#include <cstddef>
namespace viewer {
class ZombieLayer;
// Scenario travel-horde proximity, independent of visibility and occlusion.
// Ordinary zombies and dogs do not trigger this horde-noise wake-up.
// Reuses the caller's scratch storage for the existing indexed zombie query.
bool sleepThreatNearby(ZombieLayer* zombies, glm::vec3 feet,
    std::vector<std::size_t>& scratch);
}
