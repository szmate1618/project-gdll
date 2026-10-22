#pragma once

#include "zombie_layer.hpp"
#include "zombie_scenario_config.hpp"

namespace viewer {
namespace navmesh_path { class Mesh; }

struct ZombieScenarioPopulation {
    std::vector<glm::mat4> placements;
    std::vector<glm::vec3> destinations;
};

// Samples each named horde's optional spawn/destination rectangles. Omitted
// bounds use the entire navmesh, with destinations in the spawn's component.
// Bodies cannot overlap across hordes; all endpoints must be safe ground.
// Sampling is bounded and destinations must be reachable from their spawns.
ZombieScenarioPopulation generateZombieScenario(const CollisionWorld& world, const Model& scene,
                                               const navmesh_path::Mesh& mesh,
                                               const ZombieScenarioConfig& config);

// Authoritative cold load: create the population, load its character assets,
// attach navigation and calculate every route before returning the layer.
ZombieLayer loadZombieScenario(const std::filesystem::path& source,
                              const CollisionWorld& world, const Model& scene,
                              const std::filesystem::path& navmeshPath,
                              const ZombieScenarioConfig& config);

} // namespace viewer
