#pragma once

#include "player_inventory.hpp"
#include <filesystem>

namespace viewer {
// Loads player ownership, placement and shortcut configuration separately from
// equipped storage. The model constructor validates their combined layout.
InventoryContents loadInventoryContents(const std::filesystem::path& resourceRoot);
PlayerInventory loadPlayerInventory(const std::filesystem::path& resourceRoot);
} // namespace viewer
