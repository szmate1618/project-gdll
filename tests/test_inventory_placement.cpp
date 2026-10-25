#include "inventory_placement.hpp"
#include "item_config.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const viewer::StorageLoadout& storage, const viewer::InventoryItems& inventory, const char* reason) {
    try { viewer::buildInventoryLayout(storage, inventory); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(reason) != std::string::npos, "Placement error omitted its reason");
        return;
    }
    throw std::runtime_error("Invalid placement was accepted");
}
void placements() {
    const auto storage = viewer::loadStorageLoadout(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage");
    const auto inventory = viewer::loadInventoryItems(VIEWER_ITEM_ROOT);
    const auto layout = viewer::buildInventoryLayout(storage, inventory);
    require(inventory.placements.size() == 5, "Every starter weapon needs a placement");
    const auto& holster = layout.at("body/hip_holster/holster");
    require(holster.items.size() == 1 && holster.items[0].item == "pistol" &&
        holster.items[0].width == 1 && holster.items[0].height == 1, "Pistol must occupy one holster cell");
    require(layout.at("body/chest_pouch/pouch").items.at(0).item == "he_grenade", "Grenade must be in the chest pouch");
    const auto& backpack = layout.at("backpack/tactical_backpack/main");
    require(backpack.items.size() == 3 && backpack.grid.width == 8 && backpack.grid.height == 10,
        "The other three weapons must be in the equipped tactical backpack");
    require(inventory.placements.at("submachine_gun").x == 0 && inventory.placements.at("shotgun").x == 3 &&
        inventory.placements.at("sniper").y == 2, "Grid cell positions were not parsed");
    auto changed = inventory;
    changed.placements.erase("pistol");
    rejects(storage, changed, "required");
    changed = inventory;
    changed.placements.at("pistol").storage = "body/jacket/left_pocket";
    rejects(storage, changed, "unavailable storage");
    changed = inventory;
    changed.placements.at("pistol").x = 2;
    rejects(storage, changed, "outside");
    changed = inventory;
    changed.placements.at("sniper").y = 0;
    rejects(storage, changed, "overlaps");
    changed = inventory;
    changed.placements.at("sniper").y = -1;
    rejects(storage, changed, "outside");
    changed = inventory;
    changed.placements.at("sniper").y = 9;
    rejects(storage, changed, "outside");
    changed = inventory;
    changed.owned.erase("pistol");
    rejects(storage, changed, "not owned");
    auto missingEquipment = storage;
    missingEquipment.equipment.clear();
    rejects(missingEquipment, inventory, "unavailable storage");
    auto missingBackpack = storage;
    missingBackpack.backpack.reset();
    rejects(missingBackpack, inventory, "unavailable storage");
}
void boundariesAndExternalPockets() {
    const auto storage = viewer::loadStorageLoadout(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage");
    const auto original = viewer::loadInventoryItems(VIEWER_ITEM_ROOT);
    viewer::InventoryItems inventory;
    inventory.owned.emplace("pistol", original.owned.at("pistol"));
    inventory.owned.emplace("he_grenade", original.owned.at("he_grenade"));
    // Adjacent 1x1 rectangles in the two-cell holster may touch edges.
    inventory.placements["pistol"] = {"body/hip_holster/holster", 0, 0};
    inventory.placements["he_grenade"] = {"body/hip_holster/holster", 1, 0};
    require(viewer::buildInventoryLayout(storage, inventory).at("body/hip_holster/holster").items.size() == 2,
        "Touching edges must not count as overlap");
    inventory.placements["he_grenade"] = {"backpack/tactical_backpack/left_side", 0, 1};
    require(viewer::buildInventoryLayout(storage, inventory).at("backpack/tactical_backpack/left_side").items.size() == 1,
        "External backpack pockets must resolve to their own grids");
    inventory.placements["he_grenade"].y = 2;
    rejects(storage, inventory, "outside");
    inventory.placements["he_grenade"] = {"backpack/tactical_backpack/main", 7, 9};
    viewer::buildInventoryLayout(storage, inventory); // Exact bottom/right corner is valid.
    inventory.owned.at("he_grenade").width = 2;
    rejects(storage, inventory, "outside");
}
} // namespace
int main() {
    try {
        placements(); boundariesAndExternalPockets();
        std::cout << "Inventory placement checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
