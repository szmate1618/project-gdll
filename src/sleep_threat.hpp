#pragma once
#include <glm/vec3.hpp>
#include <vector>
#include <cstddef>
namespace viewer {
class ZombieLayer;
class DogLayer;
// Living threats, independent of visibility and occlusion. Reuses the caller's
// scratch storage for the existing indexed zombie query.
bool sleepThreatNearby(ZombieLayer* zombies, const DogLayer& dogs, glm::vec3 feet,
    std::vector<std::size_t>& scratch);
}
