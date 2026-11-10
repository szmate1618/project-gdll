#pragma once

#include "weapon_config.hpp"
#include "item_model.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace viewer {
struct ItemInventoryConfig {
    std::string name, type;
    int width = 0, height = 0; // Grid cells, not world dimensions.
    std::string ammoType{};
    bool stackable = false;
    int maxStack = 1;
    std::string acoustics{}; // Definition ID, retained when stacks receive instance IDs.
    // Quantities and maxStack use integer quanta: counts, or millimetres for "m".
    std::string quantityUnit = "count";
    ItemModelConfig model{};
};

int itemQuantityScale(const ItemInventoryConfig& inventory);
// Convert authored/display units to exact nonnegative quanta; no silent rounding.
std::optional<int> itemQuantityFromDisplay(const ItemInventoryConfig& inventory, double amount);
// Counts have no suffix; metre quantities include " m" and omit trailing zeros.
std::string formatItemQuantity(const ItemInventoryConfig& inventory, int amount);

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
    // Fingerprinted starting-instance ID used to reconstruct transferred stacks.
    const std::string& prototype() const { return prototype_; }
    Item renamed(const std::string& id) const;
private:
    std::string id_;
    std::string prototype_;
    ItemInventoryConfig inventory_;
    WeaponConfig gameplay_;
    std::vector<std::string> tags_;
};
bool itemStacksCompatible(const Item& source, const Item& target);
} // namespace viewer
