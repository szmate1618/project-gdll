#include "inventory/inventory_config.hpp"
#include "inventory/inventory_drag.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double value, double expected, const char* message) { require(std::abs(value - expected) < 1e-9, message); }
constexpr auto pack = "backpack/tactical_backpack/main";
constexpr auto sling = "body/left_shoulder_sling/carry";
void sourceAndDestination() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    viewer::InventoryDrag drag(inventory);
    require(drag.pickUp("sniper") && !drag.pending(), "Pickup must be immediate and start no timer");
    drag.rotate(1);
    require(drag.drop(sling, 0, 0).result == viewer::InventoryDrop::Free && drag.pending(), "Valid drop must start placement");
    near(drag.pending()->durationSeconds, 4.02, "First placement must charge 3.32s adjusted source plus 0.70s destination");
    require(inventory.held()->item == "sniper" && inventory.itemsIn(sling).empty(), "Pending placement must not commit early");
    const double half = drag.pending()->durationSeconds / 2;
    require(!drag.advance(half), "Halfway through access must not finish placement");
    near(drag.pending()->progress(), .5, "Progress must reflect elapsed access time");
    drag.rotate(1);
    require(inventory.held()->rotation == 90, "Rotation must be locked while placing");
    require(!drag.pickUp("pistol") && drag.drop(pack, 0, 6).result == viewer::InventoryDrop::Rejected,
        "Another placement must not interrupt the active operation");
    for (const double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        require(!drag.advance(invalid), "Invalid elapsed time must not advance placement");
    near(drag.pending()->elapsedSeconds, 2.01, "Invalid elapsed time changed progress");
    require(drag.advance(half) && !drag.pending() && !drag.held(), "Exact completion must commit the item");
    require(inventory.locationOf("sniper")->placement->storage == sling, "Completed placement has the wrong destination");
    drag.cancel();
    require(inventory.locationOf("sniper")->placement->storage == sling, "Cancel must preserve completed free-space moves");
}
void swapDestinationOnly() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    viewer::InventoryDrag drag(inventory);
    drag.pickUp("submachine_gun");
    require(drag.drop(pack, 3, 0).result == viewer::InventoryDrop::Swap, "First swap must be valid");
    near(drag.pending()->durationSeconds, 5.80, "Ordinary backpack swap must charge source and destination");
    require(inventory.held()->item == "submachine_gun" && inventory.locationOf("shotgun")->placement,
        "Displaced item must remain stored until the first placement completes");
    require(drag.advance(drag.pending()->durationSeconds) && inventory.held()->item == "shotgun", "Completed swap must immediately hold the displaced item");
    drag.drop(pack, 0, 2);
    near(drag.pending()->durationSeconds, 3.32, "Auto-picked shotgun must charge destination only");
    drag.advance(drag.pending()->durationSeconds);
    require(inventory.held()->item == "sniper", "Second swap must auto-pick the sniper");
    drag.rotate(1); drag.drop(sling, 0, 0);
    near(drag.pending()->durationSeconds, .7, "Auto-picked sniper must charge only sling access");
    drag.advance(.7);
    require(!inventory.held() && inventory.placements().size() == inventory.items().size(), "Timed swap chain lost an item");
}
void cancellationAndRowIndependence() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    viewer::InventoryDrag drag(inventory);
    drag.pickUp("submachine_gun"); drag.drop(pack, 3, 0); drag.advance(drag.pending()->durationSeconds);
    drag.drop(pack, 0, 2); drag.advance(1);
    drag.cancel();
    require(!drag.pending() && !drag.held() && inventory.locationOf("submachine_gun")->placement->x == 0 &&
        inventory.locationOf("shotgun")->placement->x == 3 && inventory.locationOf("sniper")->placement->y == 2,
        "Cancellation must instantly restore the full chain, including a partly timed placement");
    require(!drag.advance(100), "Cancelled operation must not complete later");
    drag.pickUp("sniper"); drag.drop(pack, 0, 8);
    near(drag.pending()->durationSeconds, 6.64, "Bottom-row placement must use adjusted compartment access");
    drag.advance(drag.pending()->durationSeconds);
    drag.pickUp("sniper"); drag.drop(pack, 0, 2);
    near(drag.pending()->durationSeconds, 6.64, "Moving between rows with identical surrounding occupied columns must preserve access");
    drag.cancel();
    require(inventory.locationOf("sniper")->placement->y == 8, "Cancelling a new timer must retain an earlier completed placement");
    drag.pickUp("he_grenade");
    require(drag.drop("body/hip_holster/holster", 0, 0).result == viewer::InventoryDrop::Rejected && !drag.pending(),
        "Rejected drops must not start timers or spend source access");
    drag.drop(pack, 7, 9);
    near(drag.pending()->durationSeconds, 3.73, "Rejected placement must preserve original pickup cost for the valid drop");
    drag.cancel();
}
} // namespace
int main() {
    try { sourceAndDestination(); swapDestinationOnly(); cancellationAndRowIndependence();
        std::cout << "Timed inventory placement checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
