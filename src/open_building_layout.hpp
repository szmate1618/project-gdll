#pragma once
#include "building_shell.hpp"
#include "house_layout.hpp"

namespace viewer {
// One neutral room per evidenced storey. Shared by small buildings and low-rise
// halls; uses the existing ramp clearance, cutout and landing rules.
// Reserved regions keep complex-section approaches open on every storey.
std::optional<house_layout::Layout> generateOpenBuildingLayout(const HouseFloorplan& plan,
    const building_shell::Shell& shell, const std::vector<house_layout::Entrance>& entrances,
    const std::vector<house_layout::Rectangle>& reserved = {});
}
