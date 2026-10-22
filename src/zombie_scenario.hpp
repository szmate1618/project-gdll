#pragma once

#include "zombie_layer.hpp"
#include "zombie_scenario_config.hpp"

namespace viewer {
namespace navmesh_path { class Mesh; }

struct ZombieScenarioPopulation {
    std::vector<glm::mat4> placements;
    std::vector<glm::vec3> destinations;
};

// Samples safe feet in the configured rectangle, with nonoverlapping bodies.
// Destinations are area-uniform samples across the spawn's entire connected
// navmesh component, independent of the spawn rectangle. Sampling is bounded.
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
