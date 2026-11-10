#pragma once

#include "item.hpp"
#include <filesystem>

namespace viewer {
// A complete custom equippable definition, with zero or more authored grips.
// The prototype records provenance; reconstruction does not require a catalogued base.
struct CraftedItemData {
    std::string id, prototype, name, acoustics;
    int width=1, height=1;
    std::vector<std::string> tags;
    ItemModelConfig model;
    std::vector<ItemGrip> grips;
};
CraftedItemData captureCraftedItem(const Item& item);
Item restoreCraftedItem(const CraftedItemData& data); // Validates all bounded geometry and metadata.

// Files use schema 2 TOML in LF form; schema 1 gripped definitions still load.
// Saving validates a temporary file before atomically replacing the definition.
void saveCraftedItem(const std::filesystem::path& path,const Item& item);
Item loadCraftedItem(const std::filesystem::path& path);
std::string craftedItemPrototype(const std::filesystem::path& path);
}
