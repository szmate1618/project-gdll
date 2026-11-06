#include "sleep_threat.hpp"
#include "sleep_plan.hpp"
#include "zombie_layer.hpp"
#include "dog_layer.hpp"
namespace viewer {
bool sleepThreatNearby(ZombieLayer* zombies, const DogLayer& dogs, glm::vec3 feet,
    std::vector<std::size_t>& scratch) {
    if (zombies) {
        zombies->queryLivingNear(feet, sleepWakeDistanceMeters, scratch);
        if (!scratch.empty()) return true;
    }
    // Dog populations are capped at 1,000 and have a separate actor index.
    for (const auto& dog : dogs.instances) {
        if (dog.removed || dog.dead) continue;
        const auto delta = glm::vec3(dog.root[3]) - feet;
        if (glm::dot(delta,delta) <= sleepWakeDistanceMeters * sleepWakeDistanceMeters) return true;
    }
    return false;
}
}
