#pragma once

#include "weapon_config.hpp"
#include <string_view>
#include <vector>

namespace viewer {
struct ItemInventoryConfig {
    std::string name, type;
    int width = 0, height = 0; // Grid cells, not world dimensions.
    std::string ammoType{};
    bool stackable = false;
    int maxStack = 1;
};

// One immutable equippable item definition, with inventory and gameplay
// components. Placement/ammo/cooldown state belongs to their runtime owners.
// IDs identify owned instances; several ammo stacks can share a definition.
class Item {
public:
    Item(std::string id, ItemInventoryConfig inventory, WeaponConfig gameplay, std::vector<std::string> tags);
    const std::string& id() const { return id_; }
    const ItemInventoryConfig& inventory() const { return inventory_; }
    const WeaponConfig& gameplay() const { return gameplay_; }
    const std::vector<std::string>& tags() const { return tags_; }
    bool hasTag(std::string_view tag) const;
private:
    std::string id_;
    ItemInventoryConfig inventory_;
    WeaponConfig gameplay_;
    std::vector<std::string> tags_;
};
} // namespace viewer
