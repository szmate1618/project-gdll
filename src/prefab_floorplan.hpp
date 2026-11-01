#pragma once

#include "building_shell.hpp"
#include "house_floorplan.hpp"

namespace viewer {
struct PrefabBlock {
    building_shell::Shell shell;
    std::size_t storeys = 0;
    bool slab = false;
};
// Shell-only classification: never reads windows, doors or other buildings.
std::optional<PrefabBlock> classifyPrefabBlock(const Model& model, const HouseSource& source);
std::size_t prefabDoorCapacity(const PrefabBlock& block, std::size_t exteriorDoors);
std::optional<HouseFloorplan> generatePrefabFloorplan(const Model& model, const HouseSource& source,
    const std::vector<DoorPanel>& entrances, const PrefabBlock& block);
} // namespace viewer
