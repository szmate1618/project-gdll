#pragma once
#include "building_shell.hpp"
#include "house_floorplan.hpp"

namespace viewer {
enum class LowRiseLayout { OpenHall, CorridorRooms, CompactRooms };
struct LowRiseBuilding {
    building_shell::Shell shell;
    LowRiseLayout preferredLayout = LowRiseLayout::OpenHall;
};
// Individual shell geometry only: 180..2200 m2, >=8 m wide, <=120 m long,
// aspect <=8, >=80% envelope fill, and wall height >=2.4 m and <12 m.
// Roof pitch does not change the lowest-eave interior ceiling.
std::optional<LowRiseBuilding> classifyLowRiseBuilding(const Model& model, const HouseSource& source);
// Uses the already-loaded window rows; reserves IDs without generating layouts.
std::size_t lowRiseDoorCapacity(const LowRiseBuilding& building, std::size_t storeys, std::size_t exteriorDoors);
std::optional<HouseFloorplan> generateLowRiseFloorplan(const Model& model, const HouseSource& source,
    const std::vector<DoorPanel>& entrances, const LowRiseBuilding& building);
}
