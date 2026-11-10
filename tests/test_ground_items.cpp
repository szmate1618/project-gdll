#include "ground_items.hpp"
#include "inventory/inventory_config.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include "camera.hpp"
#include "physics_grab.hpp"
#include "collision_box.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void compoundQueries() {
    auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    viewer::BoxSpawnMotion motion; motion.mass=2;
    const std::vector<viewer::PropCollider> parts{
        {viewer::PropColliderShape::Box,{-.4f,0,0},{.1f,.2f,.1f},viewer::propMaterialId("wood")},
        {viewer::PropColliderShape::Box,{.4f,0,0},{.1f,.2f,.1f},viewer::propMaterialId("cardboard")}};
    const auto index=boxes.addCompound({0,1,0},parts,motion);
    const auto original=boxes.prop(index);
    require(boxes.query({-1,0,-1},{1,2,1}).size()==1,"Compound broadphase queries must report each body once");
    require(std::abs(boxes.mass(index)-2)<1e-5f,"Compound part density changed the authored total mass");
    require(!boxes.raycast({0,1,1},{0,0,-1},2) && !boxes.blocksSight({0,1,1},{0,1,-1}) &&
        !boxes.groundAt(0,0,0,2),"The empty gap between parts must remain empty for rays, sight and ground support");
    const auto wood=boxes.raycast({-.4f,1,1},{0,0,-1},2), cardboard=boxes.raycast({.4f,1,1},{0,0,-1},2);
    require(wood && cardboard && wood->materialId==viewer::propMaterialId("wood") &&
        cardboard->materialId==viewer::propMaterialId("cardboard") && wood->normal.z>.99f,
        "Compound ray hits must retain each part's surface material and normal");
    std::vector<viewer::Contact> contacts;
    boxes.appendContacts({0,.8f,0},.05f,.3f,0,contacts);
    boxes.appendContacts(viewer::CollisionBox{{0,1,0},{.05f,.05f,.05f}},0,contacts);
    require(contacts.empty(),"Compound capsule/box contact queries must not fill the gap between parts");
    boxes.appendContacts({-.4f,.8f,0},.05f,.3f,0,contacts);
    require(!contacts.empty(),"Compound contacts missed an actual solid part");
    const auto ray=b3World_CastRayClosest(physics->id(),{-.4f,1,1},{0,0,-2},b3DefaultQueryFilter());
    require(ray.hit && ray.userMaterialId==viewer::propMaterialId("wood"),"Box3D shape queries lost authored material classification");
    const auto body=b3Shape_GetBody(ray.shapeId);
    require(b3Body_GetShapeCount(body)==2,"Each model part must create a separate physical shape on one body");
    physics->beginCraftingHold(body);
    contacts.clear(); boxes.appendContacts({-.4f,.8f,0},.05f,.3f,0,contacts);
    boxes.appendContacts(viewer::CollisionBox{{-.4f,1,0},{.05f,.05f,.05f}},0,contacts);
    boxes.push({-.4f,.8f,.12f},.1f,.3f,{0,0,-2},.02f);
    require(contacts.empty() && !boxes.groundAt(-.4f,0,0,2) && boxes.raycast({-.4f,1,1},{0,0,-1},2),
        "A Crafting-held prop must leave support/contact queries while remaining ray selectable");
    boxes.replaceColliders(index,{{viewer::PropColliderShape::Sphere,{},glm::vec3(.3f),0}});
    require(boxes.prop(index)==original && boxes.pose(index).halfExtents==glm::vec3(.3f),
        "Replacing preview shapes must preserve the body and update its conservative envelope");
    physics->endCraftingHold(body);
    require(std::abs(boxes.mass(index)-2)<1e-5f && b3Body_GetShapeCount(body)==1,
        "Sphere preview replacement lost the body mass or left old shapes attached");
    require(!boxes.raycast({.25f,1.25f,1},{0,0,-1},2) && !boxes.groundAt(.25f,.25f,0,2),
        "Sphere narrow queries must not use the enclosing box's corner volume");
    contacts.clear();
    boxes.appendContacts(viewer::CollisionBox{{.25f,1.25f,0},{.01f,.01f,.01f}},0,contacts);
    require(contacts.empty(),"Sphere/box contacts incorrectly filled the enclosing box's corner");
    boxes.replaceColliders(index,parts);
    require(boxes.prop(index)==original && b3Body_GetShapeCount(body)==2 &&
        !boxes.raycast({0,1,1},{0,0,-1},2),"Restoring primitive parts must restore their original empty space");
}
}
int main() {
    try {
        compoundQueries();
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
        {
            auto quickInventory = viewer::loadPlayerInventory(root);
            viewer::Weapons quickWeapons(root / "storage/weapons", root / "inventory.toml", true);
            viewer::GroundItems quick(physics);
            viewer::Camera camera; camera.setPosition({0,1.65f,0}); camera.setOrientation(-90,0);
            viewer::CollisionWorld clear(viewer::Model{});
            quick.add(*quickInventory.find("pistol_rounds_1"), 7, {}, {0,1.65f,-1});
            require(quick.pickUp(quickWeapons, quickInventory, camera, clear), "Quick pickup must acquire nearby ammo");
            require(quickInventory.ammoCount("pistol_rounds") == 127 && quickWeapons.slot() == 0,
                "Backpack pickup must preserve quantity and the selected weapon");
            bool backpack = false;
            for (const auto& [itemId, item] : quickInventory.items())
                if (itemId.compare(0,7,"ground_") == 0)
                    backpack = quickInventory.placements().at(itemId).storage == "backpack/tactical_backpack/main";
            require(backpack, "Quick pickup must use backpack space rather than a body compartment");
            quick.add(*quickInventory.find("pistol"), 0, viewer::WeaponItem{0,3,4}, {0,1.65f,-1});
            require(quick.equip(quickWeapons, quickInventory, camera, clear, {}), "Equip pickup must swap an already-owned selected weapon type");
            require(quickWeapons.slot() == 0 && quickWeapons.state(0).loaded == 3 && quickWeapons.state(0).readyAt == 4 &&
                quickInventory.owns("pistol") && quick.count() == 1, "Equip swap must preserve the incoming magazine and ownership");
            const auto outgoing = quick.poses()[0].entry;
            require(outgoing.weapon && outgoing.weapon->slot == 0 && outgoing.weapon->loaded == 12,
                "Equip swap must leave the former weapon and its magazine on the ground");
            quick.erase(outgoing.id);
            quick.add(*quickInventory.find("submachine_gun"), 0, viewer::WeaponItem{1,7,0}, {0,1.65f,-1});
            require(!quick.equip(quickWeapons, quickInventory, camera, clear, {}) && quickWeapons.slot() == 0 &&
                quickWeapons.state(0).loaded == 3 && quick.count() == 1,
                "A rejected duplicate weapon must leave the selected weapon and ground ownership unchanged");
            auto wallModel = viewer::BoxLayer::unitCube();
            wallModel.draws.push_back({0,glm::translate(glm::mat4(1),glm::vec3(0,1.65f,-.5f)) *
                glm::scale(glm::mat4(1),glm::vec3(2,2,.1f)),"wall"});
            viewer::CollisionWorld wallCollision(wallModel);
            require(!quick.pickUp(quickWeapons, quickInventory, camera, wallCollision) &&
                !quick.equip(quickWeapons, quickInventory, camera, wallCollision, {}) && quick.count() == 1,
                "Quick pickup and equip must reject an occluded item");
        }
        {
            auto modeledInventory=viewer::loadPlayerInventory(root);
            viewer::Weapons modeledWeapons(root/"storage/weapons",root/"inventory.toml",true);
            auto modeledPhysics=std::make_shared<viewer::PhysicsWorld>();
            viewer::GroundItems modeled(modeledPhysics);
            for (const auto* instance : {"duct_tape","wire_spool","nails","broom","cleaver","knife"}) {
                const auto* item=modeledInventory.find(instance);
                require(item && !item->inventory().model.parts.empty(),"A modeled starter item is unavailable");
                const auto location=modeledInventory.placements().at(instance);
                const auto quantity=modeledInventory.quantity(instance);
                const float expectedMass=item->inventory().model.massKg;
                viewer::InventoryDrag modeledDrag(modeledInventory);
                require(modeledDrag.pickUp(instance) && modeled.drop(modeledInventory,modeledDrag,modeledWeapons,
                    {0,.05f,0},{0,0,-1},collision),"Could not physically drop a modeled crafting item");
                const auto pose=modeled.poses().at(0);
                const auto prop=modeled.prop(pose.entry.id);
                require(prop && prop->valid() && std::abs(prop->mass()-expectedMass)<1e-5f,
                    "A modeled ground item lost its authored physical mass");
                if (std::string(instance)=="nails") {
                    require(std::abs(pose.body.halfExtents.y*2-.1f)<1e-6f && std::abs(pose.body.halfExtents.z-.0045f)<1e-6f,
                        "An ordinary nail must use its authored head/shaft colliders");
                    modeled.setNailPreviewSphere(*prop,true);
                    const auto preview=modeled.poses().at(0);
                    require(preview.body.halfExtents.x>.05f && preview.body.halfExtents==glm::vec3(preview.body.halfExtents.x) &&
                        modeled.prop(pose.entry.id)==prop,"Nail preview must enclose the whole nail without replacing its physical identity");
                    auto replicaPhysics=std::make_shared<viewer::PhysicsWorld>();
                    viewer::GroundItems replica(replicaPhysics,true);
                    replica.synchronize(modeled.poses());
                    require(replica.poses().at(0).body.halfExtents==preview.body.halfExtents,
                        "Replicas must reconstruct the nail's enclosing sphere from the authoritative envelope");
                    modeled.setNailPreviewSphere(*prop,false); replica.synchronize(modeled.poses());
                    require(replica.poses().at(0).body.halfExtents==pose.body.halfExtents && modeled.find(*prop),
                        "Replicas must restore primitive parts when the nail preview ends");
                }
                if (std::string(instance)=="broom")
                    require(std::abs(pose.body.halfExtents.y*2-1.3f)<1e-6f,
                        "Broom collision does not match its rendered height");
                if (std::string(instance)=="broom") {
                    const auto rayAt=[&](float x,float localY) {
                        return raycastPhysicsProp(modeledPhysics,pose.body.center+glm::vec3(x,localY,1),{0,0,-1},2);
                    };
                    require(!rayAt(.1f,.35f) && rayAt(0,.35f),"The broom handle must not inherit its broad brush width");
                }
                {
                    viewer::PhysicsGrab grip(*prop,{0,0,0},400);
                    require(grip.valid() && prop->grabbed(),"A modeled ground item cannot be physically gripped");
                    require(!modeled.take(pose.entry.id,modeledInventory,modeledWeapons,location,pose.body.center),
                        "A ground pickup must not steal a modeled item from an active hand");
                }
                require(modeled.take(pose.entry.id,modeledInventory,modeledWeapons,location,pose.body.center),
                    "A released modeled ground item cannot be picked up into inventory");
                require(modeledInventory.quantity(instance)==quantity && modeled.count()==0,
                    "A modeled ground round-trip lost quantity or left a duplicate body");
            }
        }
        std::cout << "Ground item checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
