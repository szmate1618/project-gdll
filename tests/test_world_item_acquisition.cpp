#include "ground_items.hpp"
#include "hand_inventory.hpp"
#include "collision_world.hpp"
#include "inventory/inventory_config.hpp"
#include "item_model_geometry.hpp"
#include "physics_grab.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
const std::filesystem::path root(VIEWER_ITEM_ROOT);
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
viewer::PlayerInventory starter(const viewer::PlayerInventory& catalog,int width,int height) {
    auto storage=catalog.storage(); storage.backpack->grid.width=width; storage.backpack->grid.height=height;
    viewer::InventoryContents contents; contents.skills=catalog.skills();
    contents.owned.emplace("pistol",*catalog.find("pistol"));
    contents.placements.emplace("pistol",catalog.placements().at("pistol"));
    return viewer::PlayerInventory(std::move(storage),std::move(contents));
}
struct Fixture {
    viewer::PlayerInventory catalog=viewer::loadPlayerInventory(root), inventory;
    viewer::Weapons weapons{root/"storage/weapons",root/"inventory.toml",true};
    std::shared_ptr<viewer::PhysicsWorld> world=std::make_shared<viewer::PhysicsWorld>(root/"storage");
    viewer::BoxLayer boxes{world}; viewer::GroundItems ground{world};
    viewer::CollisionWorld collision{viewer::Model{}}; viewer::DoorLayer doors{{},world};
    viewer::PlayerInteraction interaction; viewer::HandInventory hand{inventory}; viewer::Camera camera;
    Fixture(int width=8,int height=10):inventory(starter(catalog,width,height)) {
        weapons.synchronizeOwnership(inventory,0,12);
        ground.setWorldBoxes(boxes); ground.calibrateStorage(catalog);
        camera.setPosition({0,1.65f,0}); camera.setOrientation(-90,0);
    }
    std::size_t box(glm::vec3 size={.2f,.3f,.25f},float mass=2,glm::quat q={1,0,0,0}) {
        viewer::BoxSpawnMotion motion; motion.mass=mass; motion.rotation=q; motion.materialId=viewer::propMaterialId("wood");
        return boxes.add(camera.position()+camera.direction(),size,motion);
    }
    const viewer::Item& imported() const {
        for (const auto& [id,item]:inventory.items()) { (void)id; if (item.custom()) return item; }
        throw std::runtime_error("Expected one imported world item");
    }
    void fill() {
        viewer::ItemInventoryConfig config; config.name="Backpack filler"; config.type="equipment";
        config.width=inventory.storage().backpack->grid.width; config.height=inventory.storage().backpack->grid.height;
        require(inventory.insert(viewer::Item("filler",config,{},{}),0,
            {viewer::backpackStorageReference(inventory.storage().backpack->id),0,0,0}),"Could not fill the test backpack");
    }
};

void ordinaryPickupShortcutsAndFootprints() {
    for (const bool equipShortcut:{false,true}) {
        Fixture fixture; const auto index=fixture.box(); const auto source=fixture.boxes.prop(index);
        const auto selected=fixture.weapons.slot();
        const bool acquired=equipShortcut ? fixture.ground.equip(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision,{}) :
            fixture.ground.pickUp(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision);
        require(acquired && !source.valid() && !fixture.boxes.active(index) && fixture.ground.count()==0 && fixture.inventory.items().size()==2,
            "Both pickup shortcuts must transfer an ordinary crate into inventory and retire exactly its source");
        const auto& item=fixture.imported();
        require(item.custom() && !item.crafted() && item.grips().empty() && item.hasTag("wood") && item.hasTag("crafting") &&
                item.inventory().model.parts.size()==1 && std::abs(item.inventory().model.massKg-2)<1e-5f &&
                fixture.weapons.slot()==selected && !fixture.weapons.craftedItem(),
            "An imported crate must retain material/mass without inventing a combat grip or changing the equipped weapon");
    }
    glm::ivec2 first{0};
    for (int index=0;index<2;++index) {
        Fixture fixture;
        const auto q=index ? glm::angleAxis(.73f,glm::normalize(glm::vec3(1,2,3))) : glm::quat(1,0,0,0);
        viewer::BoxSpawnMotion motion; motion.mass=1;
        fixture.boxes.addCompound({0,1.65f,-1},{{viewer::PropColliderShape::Box,{}, {.0915f,.3565f,.0455f},0,q}},motion);
        require(fixture.ground.pickUp(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision),"Could not acquire the rotated-size fixture");
        const auto& item=fixture.imported(); const glm::ivec2 footprint{item.inventory().width,item.inventory().height};
        if (!index) first=footprint;
        else require(footprint==first,"Rotating identical physical geometry must not inflate its storage footprint");
    }
}

void freehandStorageAndOrdinaryEquipment() {
    Fixture fixture; const auto index=fixture.box(); const auto source=fixture.boxes.prop(index);
    require(fixture.interaction.grip(fixture.camera,fixture.boxes,source),"Could not grip the ordinary world crate");
    require(fixture.hand.store(fixture.interaction,fixture.ground,fixture.weapons) && !fixture.interaction.grabbing() &&
            !source.valid() && fixture.imported().custom(),"Returning a freehand crate to the drawer must acquire it as an item");
    const auto broom=*fixture.catalog.find("broom");
    fixture.ground.add(broom,0,{},fixture.camera.position()+fixture.camera.direction());
    const auto selected=fixture.weapons.slot();
    require(fixture.ground.equip(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision,{}) && fixture.inventory.owns("broom") &&
            !fixture.inventory.find("broom")->custom() && fixture.inventory.find("broom")->inventory().height==6 && fixture.weapons.slot()==selected,
        "H on ordinary authored equipment must store it using its authored footprint");
    require(fixture.hand.begin("broom"),"Could not retrieve ordinary equipment for the held H shortcut");
    fixture.hand.update(fixture.hand.duration(),fixture.camera,fixture.interaction,fixture.boxes,fixture.ground,fixture.weapons,fixture.collision);
    require(fixture.hand.active() && fixture.interaction.grabbing(),"Ordinary equipment retrieval must create a reserved physical hand item");
    require(fixture.hand.equip(fixture.interaction,fixture.ground,fixture.weapons,fixture.camera,fixture.collision,{}) &&
            !fixture.hand.active() && !fixture.interaction.grabbing() && fixture.inventory.owns("broom") && fixture.ground.count()==0,
        "H on a retrieved ordinary item must return it to inventory without duplicating it or requiring a combat slot");
}

void dropAndReacquireExactDefinition() {
    Fixture fixture;
    viewer::BoxSpawnMotion motion; motion.mass=2.4f;
    const auto index=fixture.boxes.addCompound({0,1.65f,-1},
        {{viewer::PropColliderShape::Box,{-.05f,0,0},{.12f,.15f,.1f},viewer::propMaterialId("wood")},
         {viewer::PropColliderShape::Box,{.12f,.12f,0},{.05f,.06f,.08f},viewer::propMaterialId("cardboard"),glm::angleAxis(.4f,glm::vec3(0,0,1))}},motion);
    fixture.ground.registerPropSource(&fixture.boxes,[&](const viewer::PhysicsProp& prop) {
        auto geometry=viewer::captureBoxPropItem(fixture.boxes,prop,"Mixed crate");
        if (geometry) geometry->model.mesh=viewer::propItemVisual(viewer::buildItemModel(geometry->model),glm::mat4(1));
        return geometry;
    },[&](const viewer::PhysicsProp& prop) {
        const auto found=fixture.boxes.indexOf(prop); if (!found) return false;
        fixture.boxes.retire(*found); return true;
    });
    require(fixture.ground.pickUp(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision),"Could not acquire the colored compound object");
    const auto item=fixture.imported();
    require(item.inventory().model.parts.size()==2 && item.inventory().model.mesh.size()==72 && item.hasTag("wood") && item.hasTag("cardboard"),
        "Importing must keep each material and the owner's actual colored visual triangles");
    viewer::InventoryDrag drag(fixture.inventory);
    require(drag.pickUp(item.id()) && fixture.ground.drop(fixture.inventory,drag,fixture.weapons,{}, {0,0,-1},fixture.collision),
        "Could not drop the acquired world item");
    require(!fixture.inventory.owns(item.id()) && fixture.ground.count()==1 && !fixture.boxes.active(index),
        "Dropping an imported item must transfer its one ownership record without reviving the old prop");
    const auto pose=fixture.ground.poses()[0]; const auto prop=*fixture.ground.prop(pose.entry.id);
    require(std::abs(prop.mass()-2.4f)<1e-4f && pose.entry.item.inventory().model.mesh[0].color==item.inventory().model.mesh[0].color &&
            pose.entry.item.inventory().model.parts[1].material=="cardboard",
        "The physical drop must preserve mass, part materials and actual visual colors");
    fixture.camera.setPosition(pose.body.center+glm::vec3(0,0,.8f)); fixture.camera.setOrientation(-90,0);
    require(fixture.ground.pickUp(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision) && fixture.ground.count()==0,
        "Could not reacquire the dropped imported item");
    const auto* reacquired=fixture.inventory.find(item.id());
    require(reacquired && reacquired->custom() && !reacquired->crafted() && reacquired->inventory().model.mesh.size()==72 &&
            reacquired->inventory().width==item.inventory().width && reacquired->inventory().height==item.inventory().height &&
            reacquired->inventory().model.mesh[5].position==item.inventory().model.mesh[5].position,
        "Reacquisition must retain the same item identity, footprint and visual geometry");
}

void rejectedAcquisitionsPreserveOwners() {
    for (int kind=0;kind<3;++kind) {
        Fixture fixture(kind==1 ? 1 : 8,kind==1 ? 1 : 10);
        if (kind==0) fixture.fill();
        const auto count=fixture.inventory.items().size();
        const auto index=fixture.box(kind==1 ? glm::vec3(.8f,.6f,.08f) : glm::vec3(.15f),
            kind==2 ? viewer::ObjectInteraction::maximumLiftMass+1 : 1);
        const auto prop=fixture.boxes.prop(index);
        require(!fixture.ground.pickUp(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision) &&
                !fixture.ground.equip(fixture.weapons,fixture.inventory,fixture.camera,fixture.collision,{}) && prop.valid() &&
                fixture.boxes.active(index) && fixture.inventory.items().size()==count && fixture.ground.count()==0,
            "Full backpacks, oversized footprints, and heavy props must reject pickup without changing either owner");
    }
    Fixture held; const auto index=held.box(); const auto prop=held.boxes.prop(index);
    {
        viewer::PhysicsGrab otherHand(prop,{},100);
        require(!held.ground.pickUp(held.weapons,held.inventory,held.camera,held.collision) &&
                !held.ground.equip(held.weapons,held.inventory,held.camera,held.collision,{}) &&
                !held.interaction.grip(held.camera,held.boxes,prop) && prop.valid() && held.inventory.items().size()==1,
            "Neither shortcut may steal another hand's physical object");
    }
    Fixture removed; const auto failed=removed.box(); const auto rejected=removed.boxes.prop(failed);
    removed.ground.registerPropSource(&removed.boxes,[&](const viewer::PhysicsProp& candidate) {
        return viewer::captureBoxPropItem(removed.boxes,candidate);
    },[](const viewer::PhysicsProp&) { return false; });
    require(!removed.ground.pickUp(removed.weapons,removed.inventory,removed.camera,removed.collision) && rejected.valid() &&
            removed.inventory.items().size()==1 && removed.ground.count()==0,
        "A source that rejects the final removal must not publish the candidate inventory item");
}
}

int main() {
    try {
        ordinaryPickupShortcutsAndFootprints(); freehandStorageAndOrdinaryEquipment();
        dropAndReacquireExactDefinition(); rejectedAcquisitionsPreserveOwners();
        std::cout<<"World item acquisition checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
