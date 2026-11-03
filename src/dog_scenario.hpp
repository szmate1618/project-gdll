#pragma once
#include "dog_layer.hpp"
#include "zombie_scenario_config.hpp"

namespace viewer {
// Seeded density-weighted safe placement, independent of zombie population RNG
// and occupancy. Every sampled position becomes that dog's territorial home.
std::vector<glm::mat4> generateDogScenario(const CollisionWorld& world, const Model& scene,
                                         const ZombieScenarioConfig& config);
DogLayer loadDogScenario(const std::filesystem::path& assets, const CollisionWorld& world,
                         const Model& scene, const ZombieScenarioConfig& config);
} // namespace viewer
