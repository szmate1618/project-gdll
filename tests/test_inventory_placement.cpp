#include "inventory/inventory_placement.hpp"
#include "inventory/inventory_config.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const viewer::StorageLoadout& storage, const viewer::InventoryContents& inventory, const char* reason) {
    try { viewer::buildInventoryLayout(storage, inventory); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(reason) != std::string::npos, "Placement error omitted its reason");
        return;
    }
    throw std::runtime_error("Invalid placement was accepted");
}
void placements() {
    const auto storage = viewer::loadStorageLoadout(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage");
    const auto inventory = viewer::loadInventoryContents(VIEWER_ITEM_ROOT);
    const auto layout = viewer::buildInventoryLayout(storage, inventory);
    require(inventory.placements.size() == inventory.owned.size(), "Every starter weapon and ammo stack needs a placement");
    for (const auto& [id, side] : {std::pair{"shotgun", "left"}, std::pair{"submachine_gun", "right"}}) {
        const auto& sling = layout.at(std::string("body/") + side + "_shoulder_sling/carry");
        require(sling.items.size() == 1 && sling.items[0].item == id && sling.items[0].width == 2 &&
            inventory.placements.at(id).rotation == 90, "Long guns must start vertically in their assigned slings");
    }
    const auto& holster = layout.at("body/hip_holster/holster");
    require(holster.items.size() == 1 && holster.items[0].item == "pistol" &&
        holster.items[0].width == 2 && holster.items[0].height == 1, "Pistol must fill the 2x1 holster");
    require(!layout.count("body/chest_pouch/pouch"), "Chest pouch must be replaced by the vest");
    const auto& vest = storage.equipment.at(1);
    require(vest.id == "tactical_vest" && vest.slots.size() == 6, "Vest must have six pouches");
    for (const auto& slot : vest.slots) {
        const auto& pouch = layout.at(viewer::bodyStorageReference(vest.id, slot.id));
        require(pouch.grid.width == 1 && pouch.grid.height == 2 && pouch.items.size() == 2,
            "Each 1x2 vest pouch must start completely full");
        for (const auto& item : pouch.items)
            require(item.width == 1 && item.height == 1 &&
                (inventory.owned.at(item.item).inventory().type == "ammo" || item.item == "he_grenade"),
                "Vest must contain only ammo stacks and grenades");
    }
    const auto& backpack = layout.at("backpack/tactical_backpack/main");
    require(backpack.items.size() == 1 && backpack.items[0].item == "sniper" &&
        backpack.items[0].width == 2 && backpack.items[0].height == 5 && inventory.placements.at("sniper").rotation == 90,
        "Sniper must start vertically in the backpack");
    auto changed = inventory;
    changed.placements.at("pistol").storage = "body/left_shoulder_sling/carry";
    rejects(storage, changed, "allowed tag");
    changed = inventory;
    const auto& pistol = changed.owned.at("pistol");
    changed.owned.insert_or_assign("pistol", viewer::Item(pistol.id(), pistol.inventory(), pistol.gameplay(), {"ranged"}));
    rejects(storage, changed, "allowed tag");
    changed = inventory;
    changed.placements.erase("pistol");
    rejects(storage, changed, "required");
    changed = inventory;
    changed.placements.at("pistol").storage = "body/jacket/left_pocket";
    rejects(storage, changed, "unavailable storage");
    changed = inventory;
    changed.placements.at("pistol").x = 2;
    rejects(storage, changed, "outside");
    changed = inventory;
    changed.placements.at("he_grenade") = changed.placements.at("sniper");
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
    const auto original = viewer::loadInventoryContents(VIEWER_ITEM_ROOT);
    viewer::InventoryContents inventory;
    inventory.owned.emplace("pistol", original.owned.at("pistol"));
    inventory.owned.emplace("he_grenade", original.owned.at("he_grenade"));
    // The 2x1 pistol and 1x1 grenade may touch edges in the backpack.
    inventory.placements["pistol"] = {"backpack/tactical_backpack/main", 0, 0};
    inventory.placements["he_grenade"] = {"backpack/tactical_backpack/main", 2, 0};
    require(viewer::buildInventoryLayout(storage, inventory).at("backpack/tactical_backpack/main").items.size() == 2,
        "Touching edges must not count as overlap");
    inventory.placements["he_grenade"] = {"backpack/tactical_backpack/left_side", 0, 1};
    require(viewer::buildInventoryLayout(storage, inventory).at("backpack/tactical_backpack/left_side").items.size() == 1,
        "External backpack pockets must resolve to their own grids");
    inventory.placements["he_grenade"].y = 2;
    rejects(storage, inventory, "outside");
    inventory.placements["he_grenade"] = {"backpack/tactical_backpack/main", 7, 9};
    viewer::buildInventoryLayout(storage, inventory); // Exact bottom/right corner is valid.
    const auto grenade = inventory.owned.at("he_grenade");
    auto footprint = grenade.inventory(); footprint.width = 2;
    inventory.owned.insert_or_assign("he_grenade", viewer::Item(grenade.id(), footprint, grenade.gameplay(), grenade.tags()));
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
