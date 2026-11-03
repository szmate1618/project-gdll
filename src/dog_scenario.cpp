#include "dog_scenario.hpp"
#include "zombie_scenario.hpp"
#include <stdexcept>

namespace viewer {
static_assert(maxDogs == dogScenarioMaximumCount,"Scenario and runtime dog limits must agree");
std::vector<glm::mat4> generateDogScenario(const CollisionWorld& world, const Model& scene,
                                         const ZombieScenarioConfig& config) {
    if (!config.dogCount()) return {};
    // Reuse the established density/repair sampler, including the conservative
    // upright-body placement clearance already used by ordinary dog spawning.
    // A separate seed stream and occupancy set leave zombie placements intact.
    ZombieScenarioConfig placement;
    if (config.seed) placement.seed = *config.seed ^ 0xd0695eedu;
    for (const auto& pack : config.dogPacks) {
        ZombieHordeConfig group;
        group.name = pack.name; group.count = pack.count;
        group.spawn = pack.spawn; group.spawnDensity = pack.spawnDensity;
        group.placementSearchRadius = pack.placementSearchRadius;
        placement.hordes.push_back(std::move(group));
    }
    try { return generateZombieScenario(world,scene,nullptr,placement).placements; }
    catch (const std::runtime_error& error) { throw std::runtime_error(std::string("Dog scenario placement: ")+error.what()); }
}
DogLayer loadDogScenario(const std::filesystem::path& assets, const CollisionWorld& world,
                         const Model& scene, const ZombieScenarioConfig& config) {
    return loadDogLayer(assets,generateDogScenario(world,scene,config));
}
} // namespace viewer
