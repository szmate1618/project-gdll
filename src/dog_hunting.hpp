#pragma once
#include <glm/vec3.hpp>
#include <filesystem>
#include <memory>

namespace viewer {
class DogLayer;
class BuildingFootprints;
namespace navmesh_path { class Mesh; }
struct DogHunting {
    enum class Phase { travel, search, returning };
    Phase phase = Phase::travel;
    int searchLegs = 0;
    float stalledSeconds = 0;
};
// Authority only. Missing companion navigation leaves ordinary dog behavior
// available; a present but invalid cache is an error, as for scenario travel.
void configureDogHunting(DogLayer& dogs, const std::filesystem::path& map,
                         const BuildingFootprints& footprints,
                         std::shared_ptr<const navmesh_path::Mesh> sharedMesh = {});
} // namespace viewer
