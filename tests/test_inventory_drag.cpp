#include "inventory_drag.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
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
    const auto storage = viewer::loadStorageLoadout(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage");
    auto inventory = viewer::loadInventoryItems(VIEWER_ITEM_ROOT);
    const auto original = inventory.placements;
    const auto shortcuts = inventory.hotbar;
    viewer::InventoryDrag drag(storage, inventory);
    require(!drag.pickUp("missing") && !drag.held(), "Missing item must not start dragging");
    require(drag.pickUp("sniper") && !drag.pickUp("pistol"), "Only one item may be held");
    require(inventory.placements.count("sniper") == 0 && inventory.owned.count("sniper") == 1, "Pickup must remove placement but retain ownership");
    const std::string sling = "body/left_shoulder_sling/carry";
    require(drag.preview(sling, 0, 0).result == viewer::InventoryDrop::Rejected, "Horizontal sniper must not fit a vertical sling");
    drag.rotate(1);
    require(drag.held()->rotation == 90 && drag.preview(sling, 0, 0).result == viewer::InventoryDrop::Free, "Quarter turn must allow the sniper in its sling");
    require(drag.drop(sling, 0, 1).result == viewer::InventoryDrop::Rejected && drag.held(), "Partial out-of-bounds placement must leave item held");
    require(drag.drop(sling, -1, 0).result == viewer::InventoryDrop::Rejected, "Negative cell must be rejected");
    require(drag.drop(sling, 0, 0).result == viewer::InventoryDrop::Free && !drag.held(), "Free drop must finish the transaction");
    const auto moved = inventory.placements;
    drag.pickUp("sniper"); drag.rotate(-1);
    require(drag.held()->rotation == 0, "Reverse wheel must turn counterclockwise");
    drag.rotate(-1); require(drag.held()->rotation == 270, "Reverse rotation must wrap");
    drag.rotate(4); require(drag.held()->rotation == 270, "Full turn must preserve orientation");
    drag.cancel();
    require(same(inventory.placements, moved) && !same(inventory.placements, original), "Cancel must preserve earlier completed moves");
    require(inventory.hotbar == shortcuts, "Storage dragging must not change hotbar assignments");
    // Configured rotated placements use the same geometry as live dragging.
    const auto layout = viewer::buildInventoryLayout(storage, inventory);
    require(layout.at(sling).items.at(0).width == 2 && layout.at(sling).items.at(0).height == 5, "Rotated footprint must be rendered vertically");
}
void swapChains() {
    viewer::StorageLoadout storage;
    viewer::StorageBackpack backpack;
    backpack.id = "test"; backpack.grid = {10, 6, 0, 0}; storage.backpack = backpack;
    const std::string pack = "backpack/test/main";
    viewer::InventoryItems inventory;
    for (const auto* id : {"a", "b", "c"}) inventory.owned[id] = {id, id, "weapon", id, 2, 1, {}};
    inventory.placements["a"] = {pack, 0, 0, 180};
    inventory.placements["b"] = {pack, 3, 0, 0};
    inventory.placements["c"] = {pack, 6, 0, 0};
    const auto original = inventory.placements;
    viewer::InventoryDrag drag(storage, inventory);
    drag.pickUp("a");
    // Only one cell overlaps B, which must still be exchanged.
    require(drag.preview(pack, 2, 0).displaced == "b", "Partial overlap must identify the displaced item");
    require(drag.drop(pack, 2, 0).result == viewer::InventoryDrop::Swap && drag.held()->item == "b", "Swap must immediately pick up B");
    require(drag.drop(pack, 5, 0).displaced == "c" && drag.held()->item == "c", "Swap chains must pick up the next item");
    require(inventory.placements.size() == 2 && inventory.owned.size() == 3, "Swap must conserve all items");
    drag.cancel();
    require(!drag.held() && same(inventory.placements, original), "Cancel must restore the entire chain, including initial rotations");
    drag.pickUp("a"); drag.drop(pack, 2, 0);
    require(drag.drop(pack, 0, 0).result == viewer::InventoryDrop::Free, "Displaced item must fit the now-vacant original area");
    const auto finished = inventory.placements;
    drag.cancel(); require(same(inventory.placements, finished), "Cancel after completing a chain must do nothing");
    drag.pickUp("c"); drag.rotate(1);
    require(drag.held()->rotation == 90, "Held rotation was not applied");
    drag.cancel(); require(same(inventory.placements, finished), "A new cancel must not undo a prior chain");
    // A wide held footprint intersects both A and B, which may not be displaced together.
    inventory.owned["c"].width = 5;
    inventory.placements["c"] = {pack, 0, 3, 0};
    viewer::InventoryDrag wide(storage, inventory);
    wide.pickUp("c");
    require(wide.drop(pack, 0, 0).result == viewer::InventoryDrop::Rejected && wide.held()->item == "c", "Overlapping multiple items must reject the drop");
    require(inventory.placements.count("a") && inventory.placements.count("b"), "Rejected drop must retain both occupied items");
    wide.cancel(); viewer::buildInventoryLayout(storage, inventory);
}
} // namespace
int main() {
    try { rotationAndBoundaries(); swapChains(); std::cout << "Inventory drag checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
