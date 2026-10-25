#include "inventory/inventory_config.hpp"
#include "inventory/inventory_drag.hpp"
#include "inventory/item_config.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::InventoryDropCheck completeDrop(viewer::InventoryDrag& drag, const std::string& storage, int x, int y) {
    const auto result = drag.drop(storage, x, y);
    if (drag.pending()) drag.advance(drag.pending()->durationSeconds);
    return result;
}
void definitionsAndQueries() {
    const std::filesystem::path root(VIEWER_ITEM_ROOT);
    const auto pistol = viewer::loadItem(root / "storage/weapons", "pistol");
    require(pistol.id() == "pistol" && pistol.inventory().width == 2 && pistol.inventory().height == 1,
        "Item must contain its inventory component");
    require(pistol.gameplay().id == "pistol" && pistol.gameplay().magazineCapacity == 12 &&
        pistol.gameplay().ammoType == "pistol_rounds" && !pistol.gameplay().automatic,
        "Item must contain the parsed gameplay component, not just its filename");
    require(pistol.hasTag("handgun") && !pistol.hasTag("long_gun"), "Item tag queries must use exact tags");
    const auto grenade = viewer::loadItem(root / "storage/weapons", "he_grenade");
    require(grenade.gameplay().grenade.has_value() && grenade.gameplay().grenade->fuseSeconds > 0,
        "Thrown weapon gameplay component was lost");
    auto inventory = viewer::loadPlayerInventory(root);
    require(inventory.owns("pistol") && !inventory.owns("missing") && inventory.find("missing") == nullptr,
        "Ownership queries must distinguish unknown IDs");
    require(inventory.hasType("weapon") && !inventory.hasType("food"), "Type queries must use inventory component types");
    require(inventory.hasTag("long_gun") && !inventory.hasTag("gun"), "Category query must use exact tags");
    const auto rifles = inventory.findByTag("long_gun");
    require(rifles.size() == 3 && inventory.findByType("weapon").size() == 5 && inventory.findByTag("food").empty(),
        "Item queries must return the matching owned items");
    for (const auto& found : rifles)
        require(found.item->hasTag("long_gun") && found.location.kind == viewer::InventoryLocationKind::Storage &&
            found.location.placement->storage == "backpack/tactical_backpack/main", "Category results must include current location");
    require(!inventory.locationOf("missing") && inventory.itemsIn("missing").empty(), "Unknown items/compartments must return no results");
    require(inventory.itemsIn("body/hip_holster/holster").at(0).item->id() == "pistol", "Compartment query must identify the stored item");
}
void queriesFollowTransactions() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    const std::string pack = "backpack/tactical_backpack/main";
    const auto sniperStart = *inventory.locationOf("sniper")->placement;
    const auto initialPackSize = inventory.itemsIn(pack).size();
    {
        viewer::InventoryDrag drag(inventory);
        drag.pickUp("sniper");
        require(inventory.owns("sniper") && inventory.locationOf("sniper")->kind == viewer::InventoryLocationKind::Hand &&
            !inventory.locationOf("sniper")->placement, "Held item must remain owned and report in-hand location");
        require(inventory.itemsIn(pack).size() + 1 == initialPackSize, "Held item must leave its storage query");
        drag.rotate(1);
        require(completeDrop(drag, "body/left_shoulder_sling/carry", 0, 0).result == viewer::InventoryDrop::Free, "Sniper move failed");
        const auto placed = inventory.locationOf("sniper");
        require(placed->kind == viewer::InventoryLocationKind::Storage && placed->placement->rotation == 90 &&
            placed->placement->storage == "body/left_shoulder_sling/carry", "Location must follow a completed rotated move");
        drag.pickUp("submachine_gun");
        completeDrop(drag, pack, 3, 0); // Exchange with the shotgun.
        require(inventory.locationOf("shotgun")->kind == viewer::InventoryLocationKind::Hand &&
            inventory.locationOf("submachine_gun")->placement->x == 3, "Location queries must reflect both sides of a swap");
        const auto primary = inventory.findByTag("long_gun");
        std::size_t inHand = 0;
        for (const auto& match : primary) inHand += match.location.kind == viewer::InventoryLocationKind::Hand;
        require(primary.size() == 3 && inHand == 1, "Category queries must include the displaced held item");
        // Controller destruction rolls back the pending swap, preserving the
        // earlier completed sniper move and leaving no orphaned held item.
    }
    require(!inventory.held() && inventory.locationOf("shotgun")->placement->x == 3 &&
        inventory.locationOf("submachine_gun")->placement->x == 0, "Transaction lifetime must restore pending placements");
    require(inventory.locationOf("sniper")->placement->storage != sniperStart.storage,
        "Cancelling a later transaction must preserve earlier completed moves");
}
void ammoQuantities() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    require(inventory.ammoCount("pistol_rounds") == 120 && inventory.ammoCount("smg_rounds") == 300 &&
        inventory.ammoCount("shotgun_shells") == 48 && inventory.ammoCount("sniper_rounds") == 30 &&
        inventory.ammoCount("he_grenades") == 5, "Starting reserves must come from the configured ammo stacks");
    const auto start = *inventory.locationOf("pistol_rounds_1")->placement;
    require(!inventory.consume("missing", 1) && !inventory.consume("pistol", 1) &&
        !inventory.consume("pistol_rounds_1", -1) && !inventory.consume("pistol_rounds_1", 61),
        "Invalid consumption must not change any stack");
    require(inventory.consume("pistol_rounds_1", 60) && inventory.quantity("pistol_rounds_1") == 0 &&
        inventory.ammoCount("pistol_rounds") == 60, "Consumption must update the selected stack and its type total");
    const auto empty = inventory.locationOf("pistol_rounds_1");
    require(inventory.owns("pistol_rounds_1") && empty->kind == viewer::InventoryLocationKind::Storage &&
        empty->placement->storage == start.storage && empty->placement->x == start.x && empty->placement->y == start.y,
        "An empty ammo stack must retain ownership and its exact storage footprint");
    require(!inventory.setQuantity("pistol_rounds_1", 61) && !inventory.setQuantity("pistol_rounds_1", -1) &&
        !inventory.setQuantity("pistol", 1) && inventory.setQuantity("pistol_rounds_1", 12),
        "Authoritative quantities must respect item stack limits");
    viewer::InventoryDrag drag(inventory);
    require(drag.pickUp("pistol_rounds_1") && inventory.consume("pistol_rounds_1", 2),
        "Dragging must preserve mutable ammo stack identity");
    drag.cancel();
    require(inventory.quantity("pistol_rounds_1") == 10 && inventory.locationOf("pistol_rounds_1")->placement->x == start.x,
        "Cancelling a drag must restore placement without resurrecting consumed ammo");
}
void constructionRejectsInvalidState() {
    const auto storage = viewer::loadStorageLoadout(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage");
    const auto original = viewer::loadInventoryContents(VIEWER_ITEM_ROOT);
    const auto rejects = [&](const viewer::InventoryContents& contents) {
        try { viewer::PlayerInventory invalid(storage, contents); }
        catch (const std::exception&) { return; }
        throw std::runtime_error("PlayerInventory accepted inconsistent ownership or locations");
    };
    auto contents = original;
    contents.placements.erase("pistol");
    rejects(contents);
    contents = original;
    contents.hotbar[9] = "missing";
    rejects(contents);
    contents = original;
    contents.owned.insert_or_assign("pistol", original.owned.at("sniper"));
    rejects(contents);
    contents = original;
    contents.quantities.erase("pistol_rounds_1");
    rejects(contents);
    contents = original;
    contents.quantities["pistol_rounds_1"] = 61;
    rejects(contents);
    contents = original;
    contents.quantities["pistol"] = 1;
    rejects(contents);
}
void authoritativePlacements() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    inventory.consume("pistol_rounds_1", 3);
    auto placements = inventory.placements();
    placements["pistol_rounds_1"] = {"backpack/tactical_backpack/top_quick_access", 0, 0};
    require(inventory.synchronizePlacements(placements) &&
        inventory.locationOf("pistol_rounds_1")->placement->storage == "backpack/tactical_backpack/top_quick_access" &&
        inventory.quantity("pistol_rounds_1") == 57, "Authoritative placements must preserve live ammo quantities");
    auto invalid = placements;
    invalid["pistol_rounds_1"] = invalid.at("pistol_rounds_2");
    require(!inventory.synchronizePlacements(invalid) &&
        inventory.locationOf("pistol_rounds_1")->placement->storage == "backpack/tactical_backpack/top_quick_access",
        "Overlapping authoritative placements must be rejected without partial updates");
    invalid = placements;
    invalid.erase("pistol");
    require(!inventory.synchronizePlacements(invalid), "Incomplete authoritative ownership must be rejected");
    invalid = placements;
    invalid["unowned"] = invalid.at("pistol");
    require(!inventory.synchronizePlacements(invalid), "Unexpected authoritative item IDs must be rejected");
    viewer::InventoryDrag drag(inventory);
    drag.pickUp("pistol_rounds_1");
    require(!inventory.synchronizePlacements(placements) && inventory.held()->item == "pistol_rounds_1",
        "Authoritative placements must not replace an active local drag");
    drag.cancel();
    require(inventory.synchronizePlacements(placements), "Completed drag cancellation must permit authoritative synchronization");
}
} // namespace
int main() {
    try { definitionsAndQueries(); queriesFollowTransactions(); ammoQuantities(); constructionRejectsInvalidState(); authoritativePlacements();
        std::cout << "Player inventory components and query checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
