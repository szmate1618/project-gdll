#include "player_medical.hpp"
#include "player_vitals.hpp"
#include "inventory/inventory_config.hpp"
#include "inventory/inventory_drag.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        using namespace viewer;
        auto inventory = loadPlayerInventory(VIEWER_ITEM_ROOT);
        require(inventory.quantity("bandages") == 4 && inventory.find("bandages")->inventory().type == "medical",
                "The starter inventory must carry four medical bandages");
        PlayerVitals player;
        player.applyDamage(.1f); player.applyDamage(.2f);
        auto& medical = player.healthState();
        const auto first = medical.injuries()[0].id(), second = medical.injuries()[1].id();
        const float health = player.health();
        require(!applyBandage(medical, inventory, "pistol", first) && !applyBandage(medical, inventory, "bandages", 999),
                "Wrong items and stale wounds cannot consume supplies");
        InventoryDrag drag(inventory);
        require(drag.pickUp("bandages"), "Bandage stack must be movable");
        require(!applyBandage(medical, inventory, "bandages", first), "An unfinished storage transaction must block treatment");
        drag.cancel();
        require(applyBandage(medical, inventory, "bandages", first) && inventory.quantity("bandages") == 3 && player.bleeding(),
                "Treatment must consume one dressing and stop only the selected wound");
        require(player.health() == health && !applyBandage(medical, inventory, "bandages", first) && inventory.quantity("bandages") == 3,
                "Repeat requests must neither heal damage nor consume another dressing");
        require(applyBandage(medical, inventory, "bandages", second) && !player.bleeding(), "Treating every wound stops blood loss");
        inventory.setQuantity("bandages", 0); player.applyDamage(.1f);
        require(!applyBandage(medical, inventory, "bandages", medical.injuries().back().id()) && player.bleeding(),
                "Empty supplies must leave the wound bleeding");
        std::cout << "Bandage inventory and treatment tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
