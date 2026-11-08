#pragma once
#include "house_floorplan.hpp"

namespace viewer {
class BuildingFootprints;
// A walkable ground slab for shells without a specialized interior. Uses the
// published roof triangulation to preserve concavity and courtyard holes.
std::optional<HouseFloorplan> generateBasicFloorplan(const Model& model, const HouseSource& source,
    const std::vector<DoorPanel>& entrances, const BuildingFootprints& footprints);
}
