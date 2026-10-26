#pragma once

#include "zombie_layer.hpp"
#include "zombie_scenario_config.hpp"

namespace viewer {
namespace navmesh_path { class Mesh; }

struct ZombieScenarioPopulation {
    std::vector<glm::mat4> placements;
    std::vector<std::optional<glm::vec3>> destinations; // Null for initially idle members; IDs match placements.
};

// Idle hordes sample safe rendered ground in their spawn rectangle or scene X/Z
// bounds, without touching navigation. Travel hordes use the navmesh and choose
// reachable destinations in their spawn's component. Bodies cannot overlap
// across either kind of horde. A null mesh is valid for idle-only scenarios.
ZombieScenarioPopulation generateZombieScenario(const CollisionWorld& world, const Model& scene,
                                               const navmesh_path::Mesh* mesh,
                                               const ZombieScenarioConfig& config);
ZombieScenarioPopulation generateZombieScenario(const CollisionWorld& world, const Model& scene,
                                               const navmesh_path::Mesh& mesh,
                                               const ZombieScenarioConfig& config);

// Authoritative cold load: create the population, load its character assets,
// attach one shared navigation mesh and preplan only travel members when needed.
ZombieLayer loadZombieScenario(const std::filesystem::path& source,
                              const CollisionWorld& world, const Model& scene,
                              const std::filesystem::path& navmeshPath,
                              const ZombieScenarioConfig& config);

} // namespace viewer
