#include "sleep_threat.hpp"
#include "sleep_plan.hpp"
#include "zombie_layer.hpp"
namespace viewer {
bool sleepThreatNearby(ZombieLayer* zombies, glm::vec3 feet,
    std::vector<std::size_t>& scratch) {
    scratch.clear();
    if (zombies) zombies->queryScenarioTravelersNear(feet, sleepWakeDistanceMeters, scratch);
    return !scratch.empty();
}
}
