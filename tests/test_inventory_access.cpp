#include "inventory_fixture.hpp"
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
void independentSwapScores() {
    auto container = backpack(6, 9);
    const viewer::InventoryItemRectangle incoming{"incoming", 1, 3, 3, 2}, outgoing{"outgoing", 3, 3, 2, 2};
    container.items = {outgoing, {"high-blocker", 1, 0, 1, 2}, {"low-blocker", 4, 1, 1, 1}, {"support", 1, 6, 3, 2}};
    const auto placed = viewer::inventoryAccess(container, incoming, outgoing.item);
    const auto picked = viewer::inventoryAccess(container, outgoing, incoming.item);
    require(placed.scores.obstruction == 2 && placed.scores.support == 2, "Incoming footprint scores are wrong");
    require(picked.scores.obstruction == 1 && picked.scores.support == 1, "Outgoing footprint scores are wrong");
    near(placed.seconds, 10.5, "Placing an item must use only its own footprint adjustments");
    near(picked.seconds, 10.25, "Auto-pick must independently retain the outgoing item's source adjustments");
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
void transferBasesAndFloor() {
    viewer::StorageGrid source{1, 4, 2}, destination = source;
    near(viewer::inventoryTransferSeconds(&source, {}, &source, {}), 2,
        "One compartment must charge its base once");
    near(viewer::inventoryTransferSeconds(&source, {}, &destination, {}), 4,
        "Distinct compartments with identical settings must each charge access");
    near(viewer::inventoryTransferSeconds(nullptr, {}, &destination, {}), 2,
        "A missing source must not add access cost");
    near(viewer::inventoryTransferSeconds(&source, {}, nullptr, {}), 2,
        "A missing destination must not add access cost");
    near(viewer::inventoryTransferSeconds(nullptr, {}, nullptr, {}), 0,
        "No storage endpoints must cost nothing");
    source.accessTime = 1; source.backpackAccess = viewer::BackpackAccessConfig{1, 1};
    near(viewer::inventoryTransferSeconds(&source, {0, 2}, &source, {3, 0}), 2,
        "Combine both signed adjustments before flooring the transfer");
    near(viewer::inventoryTransferSeconds(&source, {0, 2}, &destination, {}), 1,
        "Support must reduce the combined cost across compartments before flooring");
    near(viewer::inventoryTransferSeconds(&source, {0, 2}, &source, {0, 2}), 0,
        "The combined transfer must never have negative duration");
}
void operationSnapshots() {
    auto inventory = inventory_fixture::crowdedBackpack(VIEWER_ITEM_ROOT);
    viewer::InventoryDrag drag(inventory);
    const auto pack = "backpack/tactical_backpack/main";
    drag.pickUp("submachine_gun");
    drag.drop(pack, 3, 0);
    const auto first = *drag.pending();
    require(first.sourceAccess.scores.obstruction == 0 && first.sourceAccess.scores.support == 2,
        "Manual pickup must snapshot occupancy before the item leaves storage");
    require(first.destinationAccess.scores.obstruction == 0,
        "Swap destination must exclude the outgoing item");
    near(first.destinationAccess.scores.support, 4.0 / 3, "Swap destination must use the incoming footprint's support");
    near(first.sourceAccess.seconds, 2.84, "Source bonus was not charged exactly once");
    near(first.destinationAccess.seconds, 2.92, "Incoming destination bonus was not selected correctly");
    near(first.durationSeconds, 2.68, "Shared backpack base must be charged once with both bonuses");
    drag.advance(first.durationSeconds);
    drag.drop(pack, 0, 2);
    near(drag.pending()->sourceAccess.seconds, 2.96, "Auto-pick must defer its source access until placement");
    require(drag.pending()->sourceAccess.scores.obstruction == 0 && drag.pending()->sourceAccess.scores.support == 1,
        "Auto-pick must snapshot its own original footprint before the incoming item is published");
    require(drag.pending()->destinationAccess.scores.obstruction == 2,
        "The next placement must score the updated contents after the previous swap");
    near(drag.pending()->durationSeconds, 3.20, "Auto-picked item's next move must combine source and destination adjustments");
    drag.advance(.1); drag.cancel();
    require(inventory.locationOf("submachine_gun")->placement->x == 0 && !inventory.held(), "Cancel must restore scored/timed swap chains instantly");
}
} // namespace
int main() {
    try { columnsAndSelfExclusion(); independentSwapScores(); fixedCompartmentsAndFloor(); transferBasesAndFloor(); operationSnapshots();
        std::cout << "Inventory obstruction and support checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
