#include "action_selection.hpp"
#include "weapon_switch.hpp"
#include "inventory/inventory_config.hpp"
#include "inventory/inventory_access.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double a, double b, const char* message) { require(std::abs(a - b) < 1e-8, message); }
void selection() {
    const auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    const auto actions = viewer::actionShortcuts(inventory);
    require(actions[0].equipment == 0 && actions[5].skill && actions[5].equipment == viewer::punchSlot,
            "Hotbar must resolve both item IDs and the configured Punch skill");
    viewer::ActionSelection selection;
    selection.highlight(3);
    require(selection.selected() == 3 && selection.confirm(actions, 0) == 3 && !selection.open(),
            "A number preview must confirm exactly once");
    require(!selection.confirm(actions, 0), "A second click must not repeat confirmation");
    selection.highlight(0);
    require(!selection.confirm(actions, 0), "Confirming the current equipment must do nothing");
    selection.highlight(9);
    require(!selection.confirm(actions, 0), "An unassigned slot must not equip anything");
    selection.cycle(-1, actions, 0);
    require(selection.selected() == 5, "Wheel must wrap past empty slots to Punch");
    selection.cycle(1, actions, 0);
    require(selection.selected() == 0, "Wheel must wrap from Punch to the pistol");
    selection.close(); selection.cycle(1, {}, 0);
    require(!selection.open(), "An empty hotbar must not create a selection");
}
void transitions() {
    auto inventory = viewer::loadPlayerInventory(VIEWER_ITEM_ROOT);
    viewer::Weapons weapons(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage/weapons",
                            std::filesystem::path(VIEWER_ITEM_ROOT) / "inventory.toml", true);
    viewer::WeaponSwitch switching;
    weapons.trigger(true);
    require(!switching.begin(weapons, inventory, 0, 10) && weapons.fire(10),
            "Selecting the current weapon must not reset its trigger or charge time");
    require(switching.begin(weapons, inventory, 1, 11), "Valid weapon switch must start");
    const double duration = switching.timing()->total();
    require(duration > 1 && weapons.slot() == 0 && !weapons.fire(11), "Selection must put away the old weapon before equipping");
    require(!switching.begin(weapons, inventory, 2, 12), "Pending switches must not stack");
    near(switching.remaining(11.5), duration - .5, "Countdown must use simulation seconds");
    require(!switching.update(weapons, 11 + duration - .001) && weapons.slot() == 0,
            "A weapon must not equip before the entire delay");
    weapons.trigger(true);
    require(switching.update(weapons, 11 + duration) && weapons.slot() == 1 && !weapons.fire(11 + duration),
            "Completion must equip once without firing a click held during switching");
    require(!switching.update(weapons, 100) && !switching.pending(), "Completion must not repeat");
    const auto punch = viewer::weaponSwitchTiming(inventory, weapons.equipped().config, weapons.state(viewer::punchSlot).config);
    require(punch && punch->newAccess == 0 && punch->draw == 0 && punch->oldAccess > 0,
            "Punch must have no storage/draw cost, while putting away its old weapon stays timed");
}
void obstructionOnce() {
    const auto original = viewer::loadInventoryContents(VIEWER_ITEM_ROOT);
    auto contents = original;
    // Three distinct 1x1 items: old at the bottom, new in the middle and one blocker above.
    contents.owned.clear(); contents.placements.clear(); contents.hotbar = {};
    const auto& model = original.owned.at("pistol");
    for (const char* id : {"old", "new", "blocker"}) {
        auto config = model.gameplay(); config.id = id; config.drawTimeSeconds = .25;
        contents.owned.emplace(id, viewer::Item(id, {id, "weapon", 1, 1}, config, {}));
    }
    viewer::StorageLoadout loadout;
    loadout.backpack = viewer::StorageBackpack{};
    loadout.backpack->id = "test";
    loadout.backpack->grid = {1, 4, 2, {}, viewer::BackpackAccessConfig{.5, .25}};
    const auto reference = viewer::backpackStorageReference("test");
    contents.placements = {{"old", {reference, 0, 3}}, {"new", {reference, 0, 1}}, {"blocker", {reference, 0, 0}}};
    viewer::PlayerInventory inventory(loadout, contents);
    const auto timing = viewer::weaponSwitchTiming(inventory, inventory.find("old")->gameplay(), inventory.find("new")->gameplay());
    require(timing.has_value(), "Synthetic switch must have valid storage");
    near(timing->oldAccess + timing->newAccess, 4.5, "Pair must exclude one another and charge its shared blocker once");
    near(timing->total(), 5.0, "Total must include both draw operations and both accesses");
    // Higher obstruction belongs to the new weapon now; support also remains in force.
    contents.placements["new"].y = 2;
    contents.placements["old"].y = 0;
    contents.placements["blocker"].y = 1;
    viewer::PlayerInventory reversed(loadout, contents);
    const auto reverse = viewer::weaponSwitchTiming(reversed, reversed.find("old")->gameplay(), reversed.find("new")->gameplay());
    near(reverse->oldAccess, 1.75, "Old weapon must keep its support bonus with no obstruction charge");
    near(reverse->newAccess, 2.5, "New weapon must receive the single larger obstruction charge");
}
void theoreticalRanges() {
    viewer::StorageGrid grid{2, 3, 2, {}, viewer::BackpackAccessConfig{.5, .25}};
    const auto range = viewer::inventoryAccessRange(grid);
    double minimum = 1e9, maximum = 0;
    // Exhaust every one-cell placement and other-cell occupancy, independently
    // verifying the claimed bounds are attainable and bound all combinations.
    for (int item = 0; item < 6; ++item) for (unsigned mask = 0; mask < 64; ++mask) {
        viewer::InventoryContainer container; container.grid = grid;
        for (int cell = 0; cell < 6; ++cell)
            if (cell != item && (mask & (1u << cell)))
                container.items.push_back({std::to_string(cell), cell % 2, cell / 2, 1, 1});
        const auto access = viewer::inventoryAccess(container, {"item", item % 2, item / 2, 1, 1});
        minimum = std::min(minimum, access.seconds); maximum = std::max(maximum, access.seconds);
    }
    near(range.best, minimum, "Best theoretical access must be attainable");
    near(range.worst, maximum, "Worst theoretical access must be attainable");
    near(viewer::inventoryAccessRange(grid, 3).best, 2, "A full-height item must have no support or obstruction");
}
}
int main() {
    try { selection(); transitions(); obstructionOnce(); theoreticalRanges();
        std::cout << "Weapon selection, switching and access range checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
