#include "hand_inventory.hpp"
#include "inventory/inventory_config.hpp"
#include "collision_world.hpp"
#include <cmath>
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

        // Fill a one-cell backpack; releasing over the drawer still drops the item.
        auto storage = viewer::loadStorageLoadout(root / "storage"); storage.backpack->grid = {1,1,0};
        auto fullItem = inventory.find("pistol_rounds_1")->renamed("full");
        viewer::InventoryContents fullContents;
        fullContents.owned.emplace("full", fullItem); fullContents.quantities.emplace("full", fullItem.inventory().maxStack);
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
        require(interaction.grip(camera, boxes, *ground.prop(extraId)), "Could not regrab a rejected pickup fixture");
        require(!fullHand.pickUp(interaction, ground, weapons, camera, collision) && interaction.grabbing() &&
            ground.count() == 1 && fullHand.rejectedStores() == 2,
            "An explicit backpack pickup must preserve the physical grip when the backpack is full");
        interaction.cancel();
        require(hand.rejectedStores() == 0, "Successful returns must not report a rejection");
        {
            auto reservedContents = fullContents;
            reservedContents.owned.emplace("pistol", *inventory.find("pistol"));
            reservedContents.placements.emplace("pistol", original);
            viewer::PlayerInventory reservedInventory(storage, reservedContents);
            viewer::HandInventory reservedHand(reservedInventory);
            auto reservedWeapons = weapons;
            reservedWeapons.synchronizeOwnership(reservedInventory, 0, 12);
            viewer::GroundItems reservedGround(physics);
            viewer::PlayerInteraction reservedInteraction;
            require(reservedHand.begin("pistol"), "Could not retrieve a reserved full-backpack fixture");
            reservedHand.update(reservedHand.duration(), camera, reservedInteraction, boxes, reservedGround, reservedWeapons, collision);
            require(!reservedHand.pickUp(reservedInteraction, reservedGround, reservedWeapons, camera, collision) &&
                reservedHand.active() && reservedInteraction.grabbing() && reservedInventory.owns("pistol") &&
                reservedInventory.held() && reservedWeapons.state(0).loaded == 12 && reservedGround.count() == 1,
                "A full backpack must preserve a reserved preview transaction and its magazine");
            require(reservedHand.store(reservedInteraction, reservedGround, reservedWeapons) &&
                reservedInventory.placements().at("pistol").storage == original.storage && reservedGround.count() == 0,
                "A rejected backpack pickup must retain the original-location return privilege");
        }
        require(hand.begin("pistol"), "Could not start lost-grip fixture"); update(hand.duration());
        const auto lostId = *ground.idOf(*interaction.heldProp());
        interaction.cancel();
        require(!ground.take(lostId, otherInventory, otherWeapons, original, {0,0,0}),
            "A lost grip must stay reserved until its owning transaction observes the drop");
        update(0);
        require(!hand.active() && !inventory.owns("pistol") && !weapons.state(0).owned,
            "A lost grip must end the original-location return privilege and commit ownership");
        {
            auto swapInventory = viewer::loadPlayerInventory(root);
            viewer::Weapons swapWeapons(root / "storage/weapons", root / "inventory.toml", true);
            viewer::GroundItems swapGround(physics);
            viewer::PlayerInteraction swapInteraction;
            viewer::HandInventory swapHand(swapInventory);
            swapWeapons.equip(1);
            require(swapHand.begin("pistol"), "Could not retrieve a backpack pickup fixture");
            swapHand.update(swapHand.duration(), camera, swapInteraction, boxes, swapGround, swapWeapons, collision);
            require(swapHand.pickUp(swapInteraction, swapGround, swapWeapons, camera, collision) &&
                swapInventory.placements().at("pistol").storage == "backpack/tactical_backpack/main" &&
                swapWeapons.slot() == 1 && swapWeapons.state(0).loaded == 12 && !swapHand.active() && swapGround.count() == 0,
                "Explicit backpack pickup must move a reserved preview into the backpack without equipping it");
            require(swapHand.begin("pistol"), "Could not retrieve an equip-swap fixture");
            swapHand.update(swapHand.duration(), camera, swapInteraction, boxes, swapGround, swapWeapons, collision);
            require(swapHand.equip(swapInteraction, swapGround, swapWeapons, camera, collision, {}),
                "A reserved physical inventory preview must support equip-and-drop");
            require(!swapHand.active() && !swapInteraction.grabbing() && swapInventory.owns("pistol") &&
                !swapInventory.owns("submachine_gun") && swapWeapons.slot() == 0 && swapWeapons.state(0).loaded == 12 &&
                swapGround.count() == 1 && swapGround.poses()[0].entry.weapon->slot == 1 &&
                swapGround.poses()[0].entry.weapon->loaded == 30,
                "Equip-and-drop must commit the preview once and preserve both magazines");
            require(swapHand.begin("pistol"), "Could not retrieve a same-weapon equip fixture");
            swapHand.update(swapHand.duration(), camera, swapInteraction, boxes, swapGround, swapWeapons, collision);
            require(swapHand.equip(swapInteraction, swapGround, swapWeapons, camera, collision, {}) &&
                swapGround.count() == 1 && swapWeapons.slot() == 0 && swapInventory.owns("pistol"),
                "Equipping the selected reserved weapon must not drop an unrelated automatic fallback weapon");
        }
        {
            auto craftInventory = viewer::loadPlayerInventory(root);
            viewer::Weapons craftWeapons(root / "storage/weapons", root / "inventory.toml", true);
            viewer::GroundItems craftGround(physics);
            viewer::PlayerInteraction craftInteraction;
            viewer::HandInventory craftHand(craftInventory);
            viewer::Camera craftCamera; craftCamera.setPosition({10,2,0});
            craftInteraction.setCraftingMode(true, &craftGround);
            const int quantity = craftInventory.quantity("nails");
            require(craftHand.begin("nails", true) && craftInventory.quantity("nails") == quantity-1 &&
                    craftInventory.placements().count("nails"),
                    "Crafting must retrieve one nail while leaving its stack in inventory");
            craftHand.finishCraftingPlacement(craftInteraction,craftGround,craftWeapons);
            craftHand.update(craftHand.duration(),craftCamera,craftInteraction,boxes,craftGround,craftWeapons,collision);
            require(!craftHand.active() && craftInteraction.grabbing() && craftGround.count() == 1 &&
                    craftGround.poses()[0].entry.quantity == 1,
                    "Releasing during retrieval must park exactly one nail when access completes");
            const auto parked = craftInteraction.heldProp()->center();
            for (int frame=0;frame<60;++frame) physics->step(1.f/60);
            require(glm::distance(craftInteraction.heldProp()->center(),parked) < .001f,
                    "Inventory crafting items must remain suspended after retrieval release");
            require(craftHand.store(craftInteraction,craftGround,craftWeapons) &&
                    craftInventory.quantity("nails") == quantity && craftGround.count() == 0,
                    "Returning a placed nail must merge just one nail back into its stack");
            require(craftHand.begin("nails",true), "Could not begin cancelled nail retrieval");
            craftHand.release(craftInteraction,craftGround,craftWeapons);
            require(craftInventory.quantity("nails") == quantity,
                    "Cancelling before access completes must refund the reserved nail");
            require(craftHand.begin("nails",true), "Could not begin reserved nail return");
            craftHand.update(craftHand.duration(),craftCamera,craftInteraction,boxes,craftGround,craftWeapons,collision);
            require(craftHand.store(craftInteraction,craftGround,craftWeapons) &&
                    craftInventory.quantity("nails") == quantity && craftGround.count() == 0,
                    "Returning an active single-nail preview must restore exactly one nail");
            const auto broom = craftGround.add(*craftInventory.find("broom"),0,{},parked);
            require(craftInteraction.grip(craftCamera,boxes,*craftGround.prop(broom)), "Could not grip broom for rejected craft store");
            require(!fullHand.store(craftInteraction,craftGround,craftWeapons) && craftInteraction.grabbing(),
                    "A rejected crafting store must retain the suspended grip");
            craftInteraction.cancel();
        }
        {
            // G owns the equipped custom item even when the backpack has no
            // room. Dropping and reacquiring it must retain the saved palm.
            auto customInventory=viewer::PlayerInventory(storage,fullContents);
            viewer::Weapons customWeapons(root/"storage/weapons",root/"inventory.toml",false);
            viewer::GroundItems customGround(physics);
            viewer::PlayerInteraction customInteraction;
            viewer::HandInventory customHand(customInventory);
            auto starting=viewer::loadPlayerInventory(root);
            const auto model=starting.find("broom")->inventory().model;
            const viewer::ItemGrip grip{{.025f,.3f,-.015f},glm::angleAxis(.5f,glm::vec3(0,1,0))};
            const auto custom=starting.find("pistol")->withCrafting(model,{grip}).renamed("crafted_world_pickup");
            const auto id=customGround.add(custom,0,{},camera.position()+camera.direction()*.7f);
            require(customInteraction.grip(camera,boxes,*customGround.prop(id)),"Could not grip a world custom item");
            require(customHand.pickUp(customInteraction,customGround,customWeapons,camera,collision) &&
                    !customInteraction.grabbing() && customGround.count()==0 && customWeapons.slot()==viewer::craftedSlot &&
                    customWeapons.craftedItem() && !customInventory.owns(custom.id()) && customInventory.items().size()==1,
                    "G must equip a gripped custom item despite a full backpack");
            require(customGround.discard(customWeapons,customInventory,camera,collision,{}) &&
                    !customWeapons.craftedItem() && customGround.count()==1,
                    "Dropping a custom item must transfer the complete held copy back to the ground");
            const auto droppedPoses=customGround.poses();
            const auto& dropped=droppedPoses[0].entry;
            require(dropped.item.id()==custom.id() && dropped.item.grips().size()==1 && !dropped.weapon &&
                    glm::distance(dropped.item.grips()[0].position,grip.position)<1e-6f &&
                    std::abs(glm::dot(dropped.item.grips()[0].rotation,grip.rotation))>.999999f &&
                    dropped.item.inventory().model.parts.size()==model.parts.size(),
                    "Custom drops must retain primitive geometry and the exact grip frame");
            require(customInteraction.grip(camera,boxes,*customGround.prop(dropped.id)) &&
                    customHand.pickUp(customInteraction,customGround,customWeapons,camera,collision) &&
                    customWeapons.craftedItem()->grips().size()==1 && customGround.count()==0,
                    "A dropped crafted tool must equip again with its saved grip");
        }
        {
            auto starting=viewer::loadPlayerInventory(root);
            const auto custom=starting.find("pistol")->withCrafting(starting.find("broom")->inventory().model,
                {{{.02f,.3f,0},glm::quat(1,0,0,0)}}).renamed("crafted_reserved_preview");
            auto contents=fullContents;
            contents.owned.emplace(custom.id(),custom); contents.placements.emplace(custom.id(),original);
            viewer::PlayerInventory customInventory(storage,contents);
            viewer::Weapons customWeapons(root/"storage/weapons",root/"inventory.toml",false);
            customWeapons.synchronizeOwnership(customInventory,viewer::handlingSlot,0);
            viewer::GroundItems customGround(physics);
            viewer::PlayerInteraction customInteraction;
            viewer::HandInventory customHand(customInventory);
            require(customHand.begin(custom.id()),"Could not retrieve a reserved custom preview");
            customHand.update(customHand.duration(),camera,customInteraction,boxes,customGround,customWeapons,collision);
            require(customHand.active() && customInventory.held() && customInteraction.grabbing() && customGround.count()==1,
                    "Retrieving a custom tool must create an ordinary reserved physical preview");
            require(customHand.pickUp(customInteraction,customGround,customWeapons,camera,collision) &&
                    !customHand.active() && !customInventory.held() && !customInteraction.grabbing() && customGround.count()==0 &&
                    !customInventory.owns(custom.id()) && customWeapons.slot()==viewer::craftedSlot &&
                    customWeapons.craftedItem() && customWeapons.craftedItem()->id()==custom.id(),
                    "G must commit a reserved custom preview exactly once into the equipped hand slot when the backpack is full");
            customInventory=viewer::PlayerInventory(storage,contents);
            require(customHand.begin(custom.id()),"Could not retrieve the equipped custom item for release");
            customHand.update(customHand.duration(),camera,customInteraction,boxes,customGround,customWeapons,collision);
            customHand.release(customInteraction,customGround,customWeapons);
            require(!customInventory.owns(custom.id()) && !customWeapons.craftedItem() && customGround.count()==1,
                    "Releasing a retrieved equipped custom item must not retain a second equipped copy");
        }
        std::cout << "Hand inventory checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
