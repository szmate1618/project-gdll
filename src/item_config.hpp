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

struct InventoryItems {
    std::map<std::string, ItemDefinition> owned;
    // IDs reference owned items. Empty strings denote unassigned shortcuts.
    std::array<std::string, 10> hotbar;
};

// Reads inventory.toml ownership/hotbar and matching items/*.toml descriptors.
// Weapon behavior stays in weapons/*.toml; this snapshot is for menu display.
InventoryItems loadInventoryItems(const std::filesystem::path& resourceRoot);
} // namespace viewer
