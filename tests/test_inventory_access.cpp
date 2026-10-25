#include "inventory/inventory_access.hpp"
#include "inventory/inventory_config.hpp"
#include "inventory/inventory_drag.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double value, double expected, const char* message) { require(std::abs(value - expected) < 1e-9, message); }
viewer::InventoryContainer backpack(int width, int height) {
    viewer::InventoryContainer result;
    result.grid = {width, height, 10};
    result.grid.backpackAccess = viewer::BackpackAccessConfig{.5, .25};
    return result;
}
void columnsAndSelfExclusion() {
    auto container = backpack(5, 10);
    const viewer::InventoryItemRectangle item{"item", 1, 3, 3, 2};
    container.items = {item, {"top-left", 1, 0, 1, 2}, {"top-middle", 2, 1, 1, 1},
        {"below-left", 1, 6, 1, 3}, {"below-middle", 2, 7, 1, 1}};
    const auto access = viewer::inventoryAccess(container, item);
    require(access.scores.obstruction == 2, "Obstruction must use the worst column, excluding the entire item itself");
    near(access.scores.support, 4.0 / 3, "Support must average occupied cells below all footprint cells, including unsupported columns");
    near(access.seconds, 10 + 2 * .5 - (4.0 / 3) * .25, "Access time must apply linear penalty and support bonus");
    container.items.erase(container.items.begin());
    const auto absentSelf = viewer::inventoryAccess(container, item);
    near(absentSelf.seconds, access.seconds, "Pickup before removal and placement while held must ignore self identically");
    const auto edge = viewer::inventoryAccess(container, {"edge", 4, 0, 1, 10});
    require(edge.scores.obstruction == 0 && edge.scores.support == 0, "Grid boundary rays must stay within the grid");
    auto vertical = backpack(3, 8);
    vertical.items = {{"rotated", 1, 2, 1, 4}, {"above", 1, 0, 1, 1}, {"below", 1, 7, 1, 1}};
    const auto rotated = viewer::inventoryAccess(vertical, vertical.items.front());
    require(rotated.scores.obstruction == 1 && rotated.scores.support == 1,
        "Tall/rotated items must not count their own cells as obstruction or support");
}
void worstSwapScores() {
    auto container = backpack(6, 9);
    const viewer::InventoryItemRectangle incoming{"incoming", 1, 3, 3, 2}, outgoing{"outgoing", 3, 3, 2, 2};
    container.items = {outgoing, {"high-blocker", 1, 0, 1, 2}, {"low-blocker", 4, 1, 1, 1}, {"support", 1, 6, 3, 2}};
    const auto placed = viewer::inventoryAccess(container, incoming, outgoing.item);
    const auto picked = viewer::inventoryAccess(container, outgoing, incoming.item);
    require(placed.scores.obstruction == 2 && placed.scores.support == 2, "Incoming footprint scores are wrong");
    require(picked.scores.obstruction == 1 && picked.scores.support == 1, "Outgoing footprint scores are wrong");
    const auto swapped = viewer::inventorySwapAccess(container, incoming, outgoing);
    require(swapped.scores.obstruction == 2 && swapped.scores.support == 1,
        "Swap must independently take higher obstruction and lower support, even from different items");
    near(swapped.seconds, 10.75, "Swap must apply only one penalty, bonus and base time");
    const auto reversed = viewer::inventorySwapAccess(container, outgoing, incoming);
    near(reversed.seconds, swapped.seconds, "Unfavorable swap score selection must be symmetric");
}
void fixedCompartmentsAndFloor() {
    auto fixed = backpack(2, 3);
    fixed.grid.backpackAccess.reset(); fixed.grid.accessTime = .35;
    fixed.items = {{"above", 0, 0, 1, 1}, {"below", 0, 2, 1, 1}};
    const auto access = viewer::inventoryAccess(fixed, {"item", 0, 1, 1, 1});
    near(access.seconds, .35, "Body slots and external pockets must retain fixed access time");
    require(access.scores.obstruction == 0 && access.scores.support == 0, "Non-backpack-main storage must not use occupancy scores");
    fixed.grid.backpackAccess = viewer::BackpackAccessConfig{0, 100};
    near(viewer::inventoryAccess(fixed, {"item", 0, 1, 1, 1}).seconds, 0, "Support must not make access time negative");
}
void operationSnapshots() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    viewer::InventoryDrag drag(inventory);
    const auto pack = "backpack/tactical_backpack/main";
    drag.pickUp("submachine_gun");
    drag.drop(pack, 3, 0);
    const auto first = *drag.pending();
    require(first.sourceAccess.scores.obstruction == 0 && first.sourceAccess.scores.support == 2,
        "Manual pickup must snapshot occupancy before the item leaves storage");
    require(first.destinationAccess.scores.obstruction == 0 && first.destinationAccess.scores.support == 1,
        "Swap destination must use the lower support across both footprints");
    near(first.sourceAccess.seconds, 2.84, "Source bonus was not charged exactly once");
    near(first.destinationAccess.seconds, 2.96, "Swap destination bonus was not selected correctly");
    near(first.durationSeconds, 5.8, "Manual source plus shared swap destination total is wrong");
    drag.advance(first.durationSeconds);
    drag.drop(pack, 0, 2);
    require(drag.pending()->sourceAccess.seconds == 0 && drag.pending()->sourceAccess.scores.obstruction == 0,
        "Auto-pick must not carry a second source access or source score into the next operation");
    require(drag.pending()->destinationAccess.scores.obstruction == 2,
        "The next placement must score the updated contents after the previous swap");
    near(drag.pending()->durationSeconds, 3.32, "Auto-picked item's next move must charge only adjusted destination access");
    drag.advance(.1); drag.cancel();
    require(inventory.locationOf("submachine_gun")->placement->x == 0 && !inventory.held(), "Cancel must restore scored/timed swap chains instantly");
}
} // namespace
int main() {
    try { columnsAndSelfExclusion(); worstSwapScores(); fixedCompartmentsAndFloor(); operationSnapshots();
        std::cout << "Inventory obstruction and support checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
