#pragma once
#include "building_sections.hpp"

namespace viewer {
enum class ComplexLayout { Rooms, OpenSections };
std::size_t complexDoorCapacity(const BuildingSections& building, std::size_t storeys, std::size_t exteriorDoors);
// Sections fit in their own frames, emit world-space geometry once, and join
// only at explicit shared passages. A lower wing never receives a higher wing's
// upper floor or ceiling. OpenSections is the fallback for unfittable room plans.
std::optional<HouseFloorplan> generateComplexFloorplan(const Model& model, const HouseSource& source,
    const std::vector<DoorPanel>& entrances, const BuildingSections& building, ComplexLayout mode = ComplexLayout::Rooms);
}
