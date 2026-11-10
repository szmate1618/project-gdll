#pragma once

#include "item.hpp"
#include <filesystem>

namespace viewer {
// A complete custom equippable definition. The prototype records provenance;
// reconstruction does not require that a generated object's base be catalogued.
struct CraftedItemData {
    std::string id, prototype, name, acoustics;
    int width=1, height=1;
    std::vector<std::string> tags;
    ItemModelConfig model;
    std::vector<ItemGrip> grips;
};
CraftedItemData captureCraftedItem(const Item& item);
Item restoreCraftedItem(const CraftedItemData& data); // Validates all bounded geometry and metadata.

// Files are schema-versioned TOML, written in LF form. Saving validates a
// temporary file before atomically replacing the previous definition.
void saveCraftedItem(const std::filesystem::path& path,const Item& item);
Item loadCraftedItem(const std::filesystem::path& path);
std::string craftedItemPrototype(const std::filesystem::path& path);
}
