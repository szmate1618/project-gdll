#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace viewer {
struct ItemDefinition {
    std::string id, name, type, weapon;
    int width = 0, height = 0; // Inventory grid cells, not world dimensions.
    std::vector<std::string> tags;
};

struct ItemPlacement {
    std::string storage; // Reference to an equipped storage compartment.
    int x = 0, y = 0; // Zero-based cell coordinates of the item's top left.
};

struct InventoryItems {
    std::map<std::string, ItemDefinition> owned;
    std::map<std::string, ItemPlacement> placements;
    // IDs reference owned items. Empty strings denote unassigned shortcuts.
    std::array<std::string, 10> hotbar;
};

// Reads inventory.toml ownership/placements/hotbar and per-weapon inventory/tag
// files in storage/weapons/. Gameplay stays in *.gameplay.toml.
InventoryItems loadInventoryItems(const std::filesystem::path& resourceRoot);
} // namespace viewer
