#include "house_basement.hpp"
#include "house_layout_rules.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
void addHouseBasement(HouseFloorplan& plan, house_layout::Layout& layout,
    const HouseSource& source, const std::vector<DoorPanel>& entrances,
    const building_shell::Shell& shell) {
    std::vector<std::size_t> lowerDoors;
    float floor = plan.floor;
    for (const auto id : source.doors) {
        const auto& door = entrances.at(id);
        if (!isBasementEntrance(door, plan.floor)) continue;
        lowerDoors.push_back(id);
        // The sampled wall base is buried 8 cm; prepareDoorPanels adds a
        // 10 cm physical inset. Recover the highest terrain at this doorway.
        floor = std::min(floor, door.center.y - door.halfExtents.y - .02f);
    }
    if (lowerDoors.empty()) return;
    plan.basementFloor = floor;
    const auto room = layout.rooms.size();
    layout.rooms.push_back({HouseRoom::Kind::Living, {shell.low, shell.high},
                           std::move(lowerDoors), HouseFloorplan::basementStorey});
    // Stack the new flight directly underneath the first ground-to-upper
    // flight, with the same horizontal footprint and direction of ascent.
    const auto upward = std::find_if(layout.stairs.begin(), layout.stairs.end(),
        [&](const house_layout::Stair& stair) { return std::abs(stair.bottom - plan.floor) < .05f; });
    if (upward == layout.stairs.end()) return;
    const house_layout::rules::StairOption option{room, upward->ramp, upward->axis, upward->ascending, false};
    layout.stairs.push_back(house_layout::rules::makeStair(option, room, floor + .01f, plan.floor + .01f));
}
}
