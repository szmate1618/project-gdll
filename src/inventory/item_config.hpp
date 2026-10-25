#pragma once

#include "item.hpp"
#include <filesystem>

namespace viewer {
// Reads an inventory descriptor and tags, plus gameplay for weapons. Ammo has
// no gameplay component and may be instantiated with a separate stack ID.
// Does not read player ownership or placements.
Item loadItem(const std::filesystem::path& definitions, const std::string& id);
} // namespace viewer
