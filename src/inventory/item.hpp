#pragma once

#include "weapon_config.hpp"
#include "item_model.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace viewer {
struct ItemGrip {
    glm::vec3 position{0}; // Palm heel in model-local metres.
    // Palm-to-model rotation: +Z into the object, +Y toward fingers, +X across the palm.
    glm::quat rotation{1,0,0,0};
};
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
    // Starting definition or source-object provenance. Custom transfers also
    // carry their complete model and optional grips independently of catalog lookup.
    const std::string& prototype() const { return prototype_; }
    Item renamed(const std::string& id) const;
    Item withFootprint(int width,int height) const;
    // Mark a complete per-instance model; retain footprint, material tags and
    // existing grips. Flattened geometry replaces special canopy deployment.
    Item withModel(ItemModelConfig model) const;
    Item withCrafting(ItemModelConfig model, std::vector<ItemGrip> grips) const;
    bool custom() const { return custom_; }
    const std::vector<ItemGrip>& grips() const { return grips_; }
    bool crafted() const { return !grips_.empty(); }
private:
    std::string id_;
    std::string prototype_;
    ItemInventoryConfig inventory_;
    WeaponConfig gameplay_;
    std::vector<std::string> tags_;
    std::vector<ItemGrip> grips_;
    bool custom_=false;
};
bool itemStacksCompatible(const Item& source, const Item& target);
} // namespace viewer
