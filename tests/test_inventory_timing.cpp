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
void returningToOriginalCompartment() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    viewer::InventoryDrag drag(inventory);
    constexpr auto holster = "body/hip_holster/holster";
    constexpr auto pouch = "body/chest_pouch/pouch";
    drag.pickUp("pistol"); drag.rotate(2);
    require(drag.drop(holster, 0, 0).result == viewer::InventoryDrop::Free && !drag.pending() && !drag.held(),
        "Returning a dragged item to its holster must be instantaneous");
    require(inventory.locationOf("pistol")->placement->rotation == 180, "Free returns must still apply valid orientation changes");
    drag.pickUp("he_grenade");
    require(drag.drop(pouch, 2, 1).result == viewer::InventoryDrop::Rejected && drag.held() && !drag.pending(),
        "Free put-back must not bypass compartment bounds");
    require(drag.drop(pouch, 1, 1).result == viewer::InventoryDrop::Free && !drag.pending() && !drag.held(),
        "Returning to the original pouch must be free even in a different cell");
    require(inventory.locationOf("he_grenade")->placement->x == 1 && inventory.locationOf("he_grenade")->placement->y == 1,
        "Free returns must use the requested position");
    constexpr auto pocket = "backpack/tactical_backpack/left_side";
    drag.pickUp("he_grenade"); drag.drop(pocket, 0, 0);
    require(drag.pending() && drag.pending()->durationSeconds > 0, "Moving to a different compartment must still cost access time");
    drag.advance(drag.pending()->durationSeconds);
    drag.pickUp("he_grenade");
    require(drag.drop(pocket, 0, 1).result == viewer::InventoryDrop::Free && !drag.pending() && !drag.held(),
        "An external backpack pocket must behave like a pouch when returning its item");
    drag.pickUp("sniper"); drag.rotate(1); drag.drop(sling, 0, 0);
    drag.advance(drag.pending()->durationSeconds);
    drag.pickUp("sniper");
    require(drag.drop(sling, 0, 0).result == viewer::InventoryDrop::Free && !drag.pending(),
        "Returning an item to its shoulder sling must be free");
    drag.pickUp("submachine_gun");
    require(drag.drop(pack, 0, 0).result == viewer::InventoryDrop::Free && drag.pending() && drag.pending()->durationSeconds > 0,
        "An actual backpack drag returned to its original cells must still charge access time");
    drag.cancel();
    // Identify the main backpack by its equipped compartment, not by the
    // optional obstruction/support configuration.
    auto fixedStorage = inventory.storage(); fixedStorage.backpack->grid.backpackAccess.reset();
    viewer::PlayerInventory fixed(fixedStorage, viewer::loadInventoryContents(VIEWER_ITEM_ROOT));
    viewer::InventoryDrag fixedDrag(fixed); fixedDrag.pickUp("sniper"); fixedDrag.drop(pack, 0, 2);
    require(fixedDrag.pending() && fixedDrag.pending()->durationSeconds > 0,
        "Main-backpack returns must remain timed even without occupancy scoring");
}
} // namespace
int main() {
    try { sourceAndDestination(); swapDestinationOnly(); cancellationAndRowIndependence(); returningToOriginalCompartment();
        std::cout << "Timed inventory placement checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
