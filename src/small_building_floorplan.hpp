#pragma once
#include "house_floorplan.hpp"

namespace viewer {
// Smaller than family-house envelope/actual area or width limits:
// >=25 m2, >=4 m wide, <=2.5:1.
// Closed near-orthogonal shells only. One open room per window-derived storey,
// safe connecting ramps, and no residential subdivisions or internal doors.
std::optional<HouseFloorplan> generateSmallBuildingFloorplan(const Model& model, const HouseSource& source,
    const std::vector<DoorPanel>& entrances);
}
