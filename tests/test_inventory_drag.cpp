#include "inventory_fixture.hpp"
#include "inventory/inventory_drag.hpp"
#include "inventory/inventory_config.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::InventoryDropCheck completeDrop(viewer::InventoryDrag& drag, const std::string& storage, int x, int y) {
    const auto result = drag.drop(storage, x, y);
    if (drag.pending()) drag.advance(drag.pending()->durationSeconds);
    return result;
}
bool same(const std::map<std::string, viewer::ItemPlacement>& a, const std::map<std::string, viewer::ItemPlacement>& b) {
    if (a.size() != b.size()) return false;
    for (const auto& [id, placement] : a) {
        const auto found = b.find(id);
        if (found == b.end()) return false;
        const auto& other = found->second;
        if (placement.storage != other.storage || placement.x != other.x || placement.y != other.y || placement.rotation != other.rotation) return false;
    }
    return true;
}
void rotationAndBoundaries() {
    const auto storage = inventory_fixture::storage(VIEWER_ITEM_ROOT);
    auto inventory = inventory_fixture::crowdedBackpack(VIEWER_ITEM_ROOT);
    const auto original = inventory.placements();
    const auto shortcuts = inventory.hotbar();
    viewer::InventoryDrag drag(inventory);
    require(!drag.pickUp("missing") && !drag.held(), "Missing item must not start dragging");
    require(drag.pickUp("sniper") && !drag.pickUp("pistol"), "Only one item may be held");
    require(inventory.placements().count("sniper") == 0 && inventory.items().count("sniper") == 1, "Pickup must remove placement but retain ownership");
    const std::string sling = "body/left_shoulder_sling/carry";
    require(drag.preview(sling, 0, 0).result == viewer::InventoryDrop::Rejected, "Horizontal sniper must not fit a vertical sling");
    drag.rotate(1);
    require(drag.held()->rotation == 90 && drag.preview(sling, 0, 0).result == viewer::InventoryDrop::Free, "Quarter turn must allow the sniper in its sling");
    require(completeDrop(drag, sling, 0, 1).result == viewer::InventoryDrop::Rejected && drag.held(), "Partial out-of-bounds placement must leave item held");
    require(completeDrop(drag, sling, -1, 0).result == viewer::InventoryDrop::Rejected, "Negative cell must be rejected");
    require(completeDrop(drag, sling, 0, 0).result == viewer::InventoryDrop::Free && !drag.held(), "Free drop must finish the transaction");
    const auto moved = inventory.placements();
    drag.pickUp("sniper"); drag.rotate(-1);
    require(drag.held()->rotation == 0, "Reverse wheel must turn counterclockwise");
    drag.rotate(-1); require(drag.held()->rotation == 270, "Reverse rotation must wrap");
    drag.rotate(4); require(drag.held()->rotation == 270, "Full turn must preserve orientation");
    drag.cancel();
    require(same(inventory.placements(), moved) && !same(inventory.placements(), original), "Cancel must preserve earlier completed moves");
    require(inventory.hotbar() == shortcuts, "Storage dragging must not change hotbar assignments");
    // Configured rotated placements use the same geometry as live dragging.
    const auto layout = inventory.layout();
    require(layout.at(sling).items.at(0).width == 2 && layout.at(sling).items.at(0).height == 5, "Rotated footprint must be rendered vertically");
}
void swapChains() {
    viewer::StorageLoadout storage;
    viewer::StorageBackpack backpack;
    backpack.id = "test"; backpack.grid = {10, 6, 0}; storage.backpack = backpack;
    const std::string pack = "backpack/test/main";
    viewer::InventoryContents contents;
    for (const auto* id : {"a", "b", "c"}) {
        viewer::WeaponConfig gameplay; gameplay.id = id;
        contents.owned.emplace(id, viewer::Item(id, {id, "weapon", 2, 1}, gameplay, {}));
    }
    contents.placements["a"] = {pack, 0, 0, 180};
    contents.placements["b"] = {pack, 3, 0, 0};
    contents.placements["c"] = {pack, 6, 0, 0};
    viewer::PlayerInventory inventory(storage, contents);
    const auto original = inventory.placements();
    viewer::InventoryDrag drag(inventory);
    drag.pickUp("a");
    // Only one cell overlaps B, which must still be exchanged.
    require(drag.preview(pack, 2, 0).displaced == "b", "Partial overlap must identify the displaced item");
    require(completeDrop(drag, pack, 2, 0).result == viewer::InventoryDrop::Swap && drag.held()->item == "b", "Swap must immediately pick up B");
    require(completeDrop(drag, pack, 5, 0).displaced == "c" && drag.held()->item == "c", "Swap chains must pick up the next item");
    require(inventory.placements().size() == 2 && inventory.items().size() == 3, "Swap must conserve all items");
    drag.cancel();
    require(!drag.held() && same(inventory.placements(), original), "Cancel must restore the entire chain, including initial rotations");
    drag.pickUp("a"); completeDrop(drag, pack, 2, 0);
    require(completeDrop(drag, pack, 0, 0).result == viewer::InventoryDrop::Free, "Displaced item must fit the now-vacant original area");
    const auto finished = inventory.placements();
    drag.cancel(); require(same(inventory.placements(), finished), "Cancel after completing a chain must do nothing");
    drag.pickUp("c"); drag.rotate(1);
    require(drag.held()->rotation == 90, "Held rotation was not applied");
    drag.cancel(); require(same(inventory.placements(), finished), "A new cancel must not undo a prior chain");
    // A wide held footprint intersects both A and B, which may not be displaced together.
    contents.placements = inventory.placements();
    const auto c = contents.owned.at("c");
    contents.owned.insert_or_assign("c", viewer::Item("c", {"c", "weapon", 5, 1}, c.gameplay(), {}));
    contents.placements["c"] = {pack, 0, 3, 0};
    viewer::PlayerInventory wideInventory(storage, contents);
    viewer::InventoryDrag wide(wideInventory);
    wide.pickUp("c");
    require(completeDrop(wide, pack, 0, 0).result == viewer::InventoryDrop::Rejected && wide.held()->item == "c", "Overlapping multiple items must reject the drop");
    require(wideInventory.placements().count("a") && wideInventory.placements().count("b"), "Rejected drop must retain both occupied items");
    wide.cancel();
}
void tagRestrictions() {
    auto storage = inventory_fixture::storage(VIEWER_ITEM_ROOT);
    auto inventory = inventory_fixture::crowdedBackpack(VIEWER_ITEM_ROOT);
    const auto original = inventory.placements();
    const std::string holster = "body/hip_holster/holster";
    viewer::InventoryDrag drag(inventory);
    drag.pickUp("he_grenade");
    // The grenade fits and overlaps exactly one item, but its tag forbids a swap.
    require(drag.preview(holster, 0, 0).result == viewer::InventoryDrop::Rejected &&
        completeDrop(drag, holster, 0, 0).result == viewer::InventoryDrop::Rejected,
        "A forbidden tag must reject both preview and occupied-slot placement");
    require(drag.held()->item == "he_grenade" && inventory.placements().count("pistol"), "Rejected swap must preserve the held and stored items");
    drag.cancel(); require(same(original, inventory.placements()), "Rejected placement must be fully cancellable");
    drag.pickUp("pistol");
    for (const auto* sling : {"body/left_shoulder_sling/carry", "body/right_shoulder_sling/carry"})
        require(completeDrop(drag, sling, 0, 0).result == viewer::InventoryDrop::Rejected, "Pistol must not enter either sling even though it fits");
    drag.cancel();
    for (const auto* id : {"submachine_gun", "shotgun", "sniper"}) {
        for (const auto* sling : {"body/left_shoulder_sling/carry", "body/right_shoulder_sling/carry"}) {
            drag.pickUp(id); drag.rotate(1);
            require(completeDrop(drag, sling, 0, 0).result == viewer::InventoryDrop::Free, "Each rotated long gun must be accepted by either sling");
            drag.pickUp(id);
            const auto& start = original.at(id);
            drag.rotate(-1);
            require(completeDrop(drag, start.storage, start.x, start.y).result == viewer::InventoryDrop::Free, "Long guns must still fit unrestricted backpack storage");
        }
    }
    drag.pickUp("pistol");
    require(completeDrop(drag, holster, 0, 0).result == viewer::InventoryDrop::Free, "Matching handgun tag must allow the holster");
    // Multiple allowed tags mean any match, not all tags; this is data-driven.
    auto alternateStorage = storage;
    alternateStorage.equipment[0].slots[0].grid.allowedTags = {"utility", "grenade", "handgun"};
    viewer::PlayerInventory alternateInventory(alternateStorage, inventory_fixture::contents(VIEWER_ITEM_ROOT));
    viewer::InventoryDrag alternate(alternateInventory);
    alternate.pickUp("he_grenade");
    require(alternate.preview(holster, 0, 0).result == viewer::InventoryDrop::Swap,
        "A non-first allowed tag must be sufficient for placement");
    alternate.cancel();
}
void ammoMerging() {
    viewer::StorageLoadout storage;
    viewer::StorageBackpack backpack;
    backpack.id = "test"; backpack.grid = {4, 2, .5}; storage.backpack = backpack;
    const std::string pack = "backpack/test/main";
    viewer::InventoryContents contents;
    for (const auto* id : {"source", "target"})
        contents.owned.emplace(id, viewer::Item(id, {"Pistol rounds", "ammo", 1, 1, "pistol_rounds", true, 60}, {}, {"ammo"}));
    contents.quantities = {{"source", 10}, {"target", 54}};
    contents.placements["source"] = {pack, 0, 0};
    contents.placements["target"] = {pack, 1, 0};
    viewer::PlayerInventory inventory(storage, contents);
    viewer::InventoryDrag drag(inventory);
    require(drag.pickUp("source") && drag.drop(pack, 1, 0).result == viewer::InventoryDrop::Merge && drag.pending(),
        "Dropping compatible ammo onto a stack with room must begin a timed merge");
    require(inventory.quantity("source") == 10 && inventory.quantity("target") == 54,
        "Pending merge must not transfer rounds early");
    drag.cancel();
    require(inventory.quantity("source") == 10 && inventory.quantity("target") == 54 &&
        inventory.locationOf("source")->placement->x == 0, "Cancelling merge must preserve both stacks");
    drag.pickUp("source"); completeDrop(drag, pack, 1, 0);
    require(inventory.quantity("source") == 4 && inventory.quantity("target") == 60 && !drag.held() &&
        inventory.locationOf("source")->placement->x == 0 && inventory.locationOf("target")->placement->x == 1,
        "Partial merge must fill the target and restore the remainder to its source footprint");
    drag.pickUp("source");
    require(completeDrop(drag, pack, 1, 0).result == viewer::InventoryDrop::Merge && !drag.held() &&
        inventory.quantity("source") == 4 && inventory.quantity("target") == 60 &&
        inventory.locationOf("source")->placement->x == 0 && inventory.locationOf("target")->placement->x == 1,
        "A full target must remain a no-op merge that preserves both stack footprints");
    inventory.consume("target", 10);
    drag.pickUp("source"); completeDrop(drag, pack, 1, 0);
    require(inventory.quantity("source") == 0 && inventory.quantity("target") == 54 &&
        inventory.owns("source") && inventory.locationOf("source")->placement->x == 0 &&
        inventory.placements().size() == 2, "Complete merge must retain the empty stack and both footprints");
    drag.pickUp("source");
    require(completeDrop(drag, pack, 1, 0).result == viewer::InventoryDrop::Merge && !drag.held() &&
        inventory.quantity("source") == 0 && inventory.quantity("target") == 54 &&
        inventory.locationOf("source")->placement->x == 0 && inventory.locationOf("target")->placement->x == 1,
        "An empty source must remain a no-op merge that preserves both stack footprints");
}
} // namespace
int main() {
    try { rotationAndBoundaries(); swapChains(); tagRestrictions(); ammoMerging(); std::cout << "Inventory drag checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
