#pragma once

#include "item.hpp"
#include <filesystem>

namespace viewer {
// Reads matching .inventory.toml, .gameplay.toml and .tags.txt files.
// Does not read player ownership or placements.
Item loadItem(const std::filesystem::path& definitions, const std::string& id);
} // namespace viewer
