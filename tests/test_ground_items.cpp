#include "ground_items.hpp"
#include "inventory/inventory_config.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main() {
    try {
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        auto inventory = viewer::loadPlayerInventory(root);
        viewer::Weapons weapons(root / "storage/weapons", root / "inventory.toml", true);
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::GroundItems ground(physics);
        auto model = viewer::BoxLayer::unitCube();
        model.draws.push_back({0, glm::scale(glm::mat4(1), glm::vec3(20,.1f,20)), "floor"});
        viewer::CollisionWorld collision(model);
        viewer::PhysicsScene scene(physics, collision, {-10,-1,-10}, {10,10,10});
        const auto placement = inventory.placements().at("pistol_rounds_1");
        viewer::InventoryDrag drag(inventory);
        require(drag.pickUp("pistol_rounds_1"), "Could not pick up ammo fixture");
        require(ground.drop(inventory, drag, weapons, {0,.05f,0}, {0,0,-1}, collision), "Could not drop ammo");
        require(!inventory.owns("pistol_rounds_1") && inventory.ammoCount("pistol_rounds") == 60 && ground.count() == 1,
            "Dropped ammo must leave ownership and usable reserves");
        const auto id = ground.nearby({0,.05f,0}).at(0).id;
        const auto spawnY = ground.poses()[0].body.center.y;
        for (int i = 0; i < 120; ++i) { ground.prepare(scene); physics->step(1.f/60); }
        const auto center = ground.poses()[0].body.center;
        require(center.y < spawnY - .3f && center.y > 0, "Dropped ammo must physically fall and settle on the floor");
        require(ground.nearby(center + glm::vec3(2.07f,0,0)).size() == 1 &&
                ground.nearby(center + glm::vec3(2.09f,0,0)).empty(), "Ground reach must use the two metre collider boundary");
        require(!ground.take(id, inventory, weapons, placement, center + glm::vec3(3,0,0)), "Out-of-reach pickup succeeded");
        require(ground.take(id, inventory, weapons, placement, center), "Could not recover dropped ammo");
        require(inventory.quantity("pistol_rounds_1") == 60 && inventory.ammoCount("pistol_rounds") == 120 && ground.count() == 0,
            "Pickup lost or duplicated ammo");
        require(!ground.take(id, inventory, weapons, placement, center), "A second pickup duplicated a removed item");
        const auto foreign = ground.add(*inventory.find("pistol_rounds_1"), 7, {}, {.5f,.5f,0});
        auto freePlacement = placement; freePlacement.storage = "backpack/tactical_backpack/main"; freePlacement.x = 6; freePlacement.y = 4;
        require(ground.take(foreign, inventory, weapons, freePlacement, {0,0,0}), "A second player's same-ID ammo stack must be transferable");
        require(inventory.ammoCount("pistol_rounds") == 127, "Renaming an acquired stack lost its quantity");
        bool renamed = false;
        for (const auto& [itemId, item] : inventory.items())
            if (itemId.compare(0,7,"ground_") == 0) renamed = item.prototype() == "pistol_rounds_1" && inventory.quantity(itemId) == 7;
        require(renamed, "Transferred ammo must retain its fingerprinted prototype");
        const auto pistolPlacement = inventory.placements().at("pistol");
        const int loaded = weapons.state(0).loaded;
        require(drag.pickUp("pistol") && ground.drop(inventory, drag, weapons, {0,.05f,0}, {1,0,0}, collision), "Could not drop pistol");
        require(!weapons.state(0).owned && !inventory.owns("pistol"), "Dropped weapon remained usable");
        require(ground.take(ground.nearby({0,0,0})[0].id, inventory, weapons, pistolPlacement, {0,0,0}), "Could not recover pistol");
        require(weapons.state(0).owned && weapons.state(0).loaded == loaded, "Weapon magazine changed on round trip");
        auto wall = viewer::BoxLayer::unitCube();
        wall.draws.push_back({0,glm::translate(glm::mat4(1),glm::vec3(0,.7f,-.4f)) * glm::scale(glm::mat4(1),glm::vec3(2,2,.1f)),"wall"});
        viewer::CollisionWorld blocked(wall);
        require(drag.pickUp("pistol_rounds_1") && !ground.drop(inventory,drag,weapons,{0,.05f,0},{0,0,-1},blocked),
            "A drop through a wall must be rejected");
        require(inventory.owns("pistol_rounds_1") && drag.held(), "Rejected drop lost the held item");
        drag.cancel();
        auto predictedInventory = inventory;
        auto predictedWeapons = weapons;
        viewer::InventoryDrag predictedDrag(predictedInventory);
        viewer::GroundItems predicted(std::make_shared<viewer::PhysicsWorld>(),true);
        require(predictedDrag.pickUp("pistol_rounds_1") && predicted.drop(predictedInventory,predictedDrag,predictedWeapons,{0,.05f,0},{0,0,-1},collision),
            "Could not predict a client ground drop");
        const auto predictedId = predicted.nearby({0,0,0})[0].id;
        require(predicted.pending(predictedId) && !predicted.take(predictedId,predictedInventory,predictedWeapons,placement,{0,0,0}),
            "A predicted drop must wait for its authoritative ID before another pickup can address it");
        std::cout << "Ground item checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
