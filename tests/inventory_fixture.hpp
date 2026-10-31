#pragma once

#include "inventory/inventory_config.hpp"

namespace inventory_fixture {
// Fixed crowded backpack for swap, obstruction and pointer tests. Keep these
// scenarios independent of changes to the player's starting loadout.
inline viewer::StorageLoadout storage(const std::filesystem::path& root) {
    auto result = viewer::loadStorageLoadout(root / "storage");
    result.equipment.at(0).slots.at(0).x = -.8;
    result.equipment.at(0).slots.at(0).y = .45;
    result.equipment.at(1) = {"chest_pouch", "Chest pouch",
        {{"pouch", "CHEST POUCH", .75, .1, {2, 2, .65}}}};
    return result;
}

inline viewer::InventoryContents contents(const std::filesystem::path& root) {
    auto result = viewer::loadInventoryContents(root);
    const auto pack = "backpack/tactical_backpack/main";
    result.placements.at("pistol") = {"body/hip_holster/holster", 0, 0};
    result.placements.at("he_grenade") = {"body/chest_pouch/pouch", 0, 0};
    result.placements.at("submachine_gun") = {pack, 0, 0};
    result.placements.at("shotgun") = {pack, 3, 0};
    result.placements.at("sniper") = {pack, 0, 2};
    result.placements.at("pistol_rounds_1") = {pack, 7, 0};
    result.placements.at("pistol_rounds_2") = {pack, 7, 1};
    for (int index = 1; index <= 5; ++index)
        result.placements.at("smg_rounds_" + std::to_string(index)) = {pack, 7, index + 1};
    result.placements.at("shotgun_shells_1") = {pack, 7, 7};
    result.placements.at("shotgun_shells_2") = {pack, 7, 8};
    result.placements.at("sniper_rounds_1") = {"backpack/tactical_backpack/right_side", 0, 0};
    result.placements.at("he_grenades_1") = {"backpack/tactical_backpack/right_side", 0, 1};
    return result;
}

inline viewer::PlayerInventory crowdedBackpack(const std::filesystem::path& root) {
    return {storage(root), contents(root)};
}
} // namespace inventory_fixture
