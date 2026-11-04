#include "hand_inventory.hpp"
#include "inventory/inventory_config.hpp"
#include "collision_world.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        auto inventory = viewer::loadPlayerInventory(root);
        viewer::Weapons weapons(root / "storage/weapons", root / "inventory.toml", true);
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::BoxLayer boxes(physics);
        viewer::GroundItems ground(physics);
        viewer::CollisionWorld collision(viewer::Model{});
        viewer::DoorLayer doors({}, physics);
        viewer::PlayerInteraction interaction;
        viewer::HandInventory hand(inventory);
        viewer::Camera camera;
        camera.setPosition({0,1.65f,0});
        const auto original = inventory.placements().at("pistol");
        const auto update = [&](double seconds) { hand.update(seconds, camera, interaction, boxes, ground, weapons, collision); };
        {
            viewer::InventoryDrag page(inventory);
            require(page.pickUp("pistol") && !hand.active(), "A full-page drag must not become a physical hand transaction");
            hand.release(interaction, ground, weapons);
            require(page.held().has_value(), "Releasing an inactive physical hand must not cancel the full-page transaction");
            page.cancel();
        }
        require(hand.begin("pistol"), "Could not start retrieval");
        const auto expected = viewer::inventoryAccess(inventory.layout().at(original.storage),
            viewer::inventoryRectangle(*inventory.find("pistol"), original)).seconds;
        require(hand.duration() == expected && expected > 0, "Retrieval must use the item's calculated source access");
        update(expected * .5);
        require(ground.count() == 0 && !interaction.grabbing(), "Retrieval created a physical grip before access completed");
        hand.release(interaction, ground, weapons);
        require(!hand.active() && inventory.placements().at("pistol").storage == original.storage && ground.count() == 0,
            "Releasing an unfinished retrieval must restore inventory without dropping an item");
        require(hand.begin("pistol"), "Could not restart retrieval"); update(expected);
        require(interaction.heldProp() && ground.count() == 1 && hand.active(), "Completed retrieval must create an ordinary physical grip");
        const auto heldId = ground.poses()[0].entry.id;
        auto otherInventory = viewer::loadPlayerInventory(root);
        viewer::InventoryDrag otherDrag(otherInventory); require(otherDrag.pickUp("pistol"), "Could not create foreign fixture");
        require(otherDrag.release(), "Could not release foreign fixture");
        auto otherWeapons = weapons; otherWeapons.discard(0);
        require(!ground.take(heldId, otherInventory, otherWeapons, original, {0,0,0}), "A reserved physical preview was acquired by another player");
        interaction.update(camera, .1f, doors, boxes, collision, .1); update(.1);
        require(hand.store(interaction, ground, weapons), "Could not return a continuously held item");
        const auto restored = inventory.placements().at("pistol");
        require(restored.storage == original.storage && restored.x == original.x && restored.y == original.y &&
            restored.rotation == original.rotation && ground.count() == 0 && weapons.state(0).loaded == 12,
            "Continuously held items must return to their exact original location with their magazine intact");
        require(hand.begin("pistol"), "Could not retrieve removed-body fixture"); update(expected);
        ground.erase(ground.poses()[0].entry.id); update(0);
        require(!hand.active() && inventory.owns("pistol") && weapons.state(0).owned,
            "A removed preview body must restore inventory and weapon ownership");
        require(hand.begin("pistol"), "Could not retrieve for drop"); update(expected);
        hand.release(interaction, ground, weapons);
        require(!inventory.owns("pistol") && !weapons.state(0).owned && ground.count() == 1,
            "Mouse release must transfer inventory and weapon ownership to the ground exactly once");
        const auto droppedId = ground.poses()[0].entry.id;
        require(interaction.grip(camera, boxes, *ground.prop(droppedId)), "Could not regrab dropped item");
        require(hand.store(interaction, ground, weapons), "Could not store regrabbed item");
        require(inventory.placements().at("pistol").storage == "backpack/tactical_backpack/main" &&
            weapons.state(0).owned && weapons.state(0).loaded == 12 && ground.count() == 0,
            "An item regrabbed after a drop must use suitable backpack space, preserving its magazine");

        const auto raisedId = ground.add(*inventory.find("pistol_rounds_1"), 7, {}, {1.4f,2.5f,-.7f});
        require(interaction.grip(camera, boxes, *ground.prop(raisedId)), "Could not grip lifted-item fixture");
        require(hand.store(interaction, ground, weapons) && ground.count() == 0 && inventory.ammoCount("pistol_rounds") == 127,
            "A held item lifted beyond ground-query reach must still fit into the backpack");

        // Fill a one-cell backpack; a regrabbed item must stay physical on release.
        auto storage = viewer::loadStorageLoadout(root / "storage"); storage.backpack->grid = {1,1,0};
        auto fullItem = inventory.find("pistol_rounds_1")->renamed("full");
        viewer::InventoryContents fullContents;
        fullContents.owned.emplace("full", fullItem); fullContents.quantities.emplace("full", 1);
        fullContents.placements.emplace("full", viewer::ItemPlacement{"backpack/tactical_backpack/main",0,0,0});
        viewer::PlayerInventory full(storage, fullContents);
        viewer::HandInventory fullHand(full);
        const auto extraId = ground.add(*inventory.find("pistol_rounds_1"), 7, {}, {0,1,0});
        require(interaction.grip(camera, boxes, *ground.prop(extraId)), "Could not grab full-backpack fixture");
        require(!fullHand.store(interaction, ground, weapons) && !interaction.grabbing() && ground.count() == 1 &&
            full.items().size() == 1, "A full backpack must release the physical item without changing ownership");
        require(fullHand.rejectedStores() == 1, "A rejected return must publish feedback exactly once");
        require(!fullHand.store(interaction, ground, weapons) && fullHand.rejectedStores() == 1,
            "Releasing an empty hand must not report an inventory rejection");
        require(hand.rejectedStores() == 0, "Successful returns must not report a rejection");
        require(hand.begin("pistol"), "Could not start lost-grip fixture"); update(hand.duration());
        const auto lostId = *ground.idOf(*interaction.heldProp());
        interaction.cancel();
        require(!ground.take(lostId, otherInventory, otherWeapons, original, {0,0,0}),
            "A lost grip must stay reserved until its owning transaction observes the drop");
        update(0);
        require(!hand.active() && !inventory.owns("pistol") && !weapons.state(0).owned,
            "A lost grip must end the original-location return privilege and commit ownership");
        std::cout << "Hand inventory checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
