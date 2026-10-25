#pragma once

#include "storage_config.hpp"
#include <map>
#include <string>
#include <vector>

namespace viewer {
struct InventoryItems;
struct InventoryItemRectangle {
    std::string item;
    int x = 0, y = 0, width = 0, height = 0; // Grid cells, top left origin.
};
struct InventoryContainer {
    StorageGrid grid;
    std::vector<InventoryItemRectangle> items;
};
using InventoryLayout = std::map<std::string, InventoryContainer>;

std::string bodyStorageReference(const std::string& equipment, const std::string& slot);
// "main" is reserved for the backpack's internal grid.
std::string backpackStorageReference(const std::string& backpack, const std::string& slot = "main");

// Resolves equipped storage references and requires exactly one in-bounds,
// non-overlapping placement per owned item. No GPU state or item interaction.
InventoryLayout buildInventoryLayout(const StorageLoadout& loadout, const InventoryItems& inventory);
} // namespace viewer
