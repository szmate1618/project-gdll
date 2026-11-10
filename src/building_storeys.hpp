#pragma once
#include "house_floorplan.hpp"

namespace viewer {
struct BuildingStoreys {
    float floor, ceiling;
    std::vector<float> upperFloors;
};
// Runtime Y metres. Prepared main-door heads fix the floor datum; window rows
// supply upper floors. Without additional rows, no storeys are invented.
std::optional<BuildingStoreys> inferBuildingStoreys(float eaves, const std::vector<float>& windowHeads,
    const HouseSource& source, const std::vector<DoorPanel>& entrances, float maximumHeight = 10);
}
