#pragma once

#include "storage_config.hpp"
#include <map>
#include <string>
#include <vector>

namespace viewer {
struct InventoryContents;
class Item;
struct ItemPlacement;
struct InventoryItemRectangle {
    std::string item;
    int x = 0, y = 0, width = 0, height = 0; // Grid cells, top left origin.
};
struct InventoryContainer {
    StorageGrid grid;
    std::vector<InventoryItemRectangle> items;
};
using InventoryLayout = std::map<std::string, InventoryContainer>;
InventoryItemRectangle inventoryRectangle(const Item& item, const ItemPlacement& placement);
bool inventoryItemAllowed(const StorageGrid& grid, const Item& item);
bool inventoryRectangleFits(const StorageGrid& grid, const InventoryItemRectangle& rectangle);
bool inventoryRectanglesOverlap(const InventoryItemRectangle& a, const InventoryItemRectangle& b);

std::string bodyStorageReference(const std::string& equipment, const std::string& slot);
// "main" is reserved for the backpack's internal grid.
std::string backpackStorageReference(const std::string& backpack, const std::string& slot = "main");

// Resolves equipped storage references and requires exactly one in-bounds,
// non-overlapping placement per owned item. No GPU state or item interaction.
// A held item temporarily has no placement. All other ownership invariants hold.
InventoryLayout buildInventoryLayout(const StorageLoadout& loadout, const InventoryContents& inventory,
                                     const std::string& heldItem = {});
} // namespace viewer
