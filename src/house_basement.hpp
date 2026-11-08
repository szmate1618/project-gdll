#pragma once

#include "house_layout.hpp"
#include "building_shell.hpp"

namespace viewer {
inline bool isBasementEntrance(const DoorPanel& door, float floor) {
    return door.center.y + door.halfExtents.y < floor - .01f;
}
// Lower exterior leaves beneath the facade floor identify balcony buildings.
// Keep ground/upper layout choices and runtime door reservations unchanged.
void addHouseBasement(HouseFloorplan& plan, house_layout::Layout& layout,
    const HouseSource& source, const std::vector<DoorPanel>& entrances,
    const building_shell::Shell& shell);
}
