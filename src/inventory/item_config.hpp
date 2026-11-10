#pragma once

#include "item.hpp"
#include <filesystem>

namespace viewer {
// Reads an inventory descriptor (including optional model and inline tags),
// falling back to tag sidecars, plus gameplay for weapons. Stack instances have
// no gameplay component and may use a separate instance ID.
// Does not read player ownership or placements.
Item loadItem(const std::filesystem::path& definitions, const std::string& id);
} // namespace viewer
