#include "open_building_layout.hpp"
#include "house_layout_rules.hpp"

namespace viewer {
std::optional<house_layout::Layout> generateOpenBuildingLayout(const HouseFloorplan& plan,
        const building_shell::Shell& shell, const std::vector<house_layout::Entrance>& entrances,
        const std::vector<house_layout::Rectangle>& reserved) {
    const house_layout::Rectangle footprint{shell.low, shell.high};
    std::vector<std::size_t> mainDoors;
    for (const auto& entrance : entrances) mainDoors.push_back(entrance.id);
    house_layout::Layout layout;
    for (std::size_t level = 0; level <= plan.upperFloors.size(); ++level) {
        const float floor = plan.floorHeight(level);
        const float ceiling = plan.ceilingHeight(level);
        if (ceiling - (level == 0 ? plan.floor : floor) < 2.4f) return {};
        const auto room = layout.rooms.size();
        layout.rooms.push_back({HouseRoom::Kind::OpenRoom, footprint, level == 0 ? mainDoors : std::vector<std::size_t>{}, level});
        if (level == plan.upperFloors.size()) break;
        auto blocked = reserved;
        if (!layout.stairs.empty()) {
            const auto& incoming = layout.stairs.back();
            auto landing = incoming.ramp;
            if (incoming.ascending) {
                landing.low[incoming.axis] = incoming.ramp.high[incoming.axis];
                landing.high[incoming.axis] += .8f;
            } else {
                landing.high[incoming.axis] = incoming.ramp.low[incoming.axis];
                landing.low[incoming.axis] -= .8f;
            }
            blocked.push_back(incoming.opening); blocked.push_back(landing);
        }
        const auto options = house_layout::rules::stairOptions({layout.rooms.back()}, {},
            level == 0 ? entrances : std::vector<house_layout::Entrance>{}, footprint, ceiling - floor, blocked, shell.outline);
        if (options.empty()) return {};
        layout.stairs.push_back(house_layout::rules::makeStair(options.front(), room, floor, ceiling));
    }
    return layout;
}
}
