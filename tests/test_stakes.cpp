#include "stake_projectiles.hpp"
#include "inventory/inventory_config.hpp"
#include "inventory/item_config.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include "physics_grab.hpp"
#include "camera.hpp"
#include "sword_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const std::filesystem::path root(VIEWER_ITEM_ROOT);
viewer::Item stake() { return viewer::loadItem(root / "storage/weapons","stake"); }
viewer::Model floor() {
    auto model = viewer::BoxLayer::unitCube();
    model.draws.push_back({0,glm::scale(glm::mat4(1),{30,.1f,30}),"floor"});
    return model;
}
void flightAndGround() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::GroundItems items(world);
    viewer::CollisionWorld collision(floor());
    viewer::PhysicsScene scene(world,collision,{-15,-1,-15},{15,20,15});
    viewer::StakeProjectiles flight(items,world,collision);
    const auto id = items.add(stake(),0,viewer::WeaponItem{viewer::stakeSlot,0,0},{0,3,0},{0,-30,0});
    for (int i=0; i<20 && !flight.embedded(id); ++i) { items.prepare(scene); world->step(1.f/60); }
    require(flight.embedded(id),"Fast downward tip must embed in ground");
    auto pose = items.poses()[0].body;
    require((pose.rotation * glm::vec3(0,0,-1)).y < -.999f,"Vertical flight must point downward without singularities");
    const auto tip = pose.center + pose.rotation * glm::vec3(0,0,-.75f);
    require(std::abs(tip.y + .03f) < .03f,"Only the point should penetrate the rendered floor");
    const auto pinnedCenter = pose.center;
    for (int i=0; i<60; ++i) world->step(1.f/60);
    require(glm::distance(items.poses()[0].body.center,pinnedCenter) < .01f,"Embedded ground stake must stay fixed");
    const auto prop = *items.prop(id);
    require(viewer::raycastPhysicsProp(world,pose.center+glm::vec3(1,0,0),{-1,0,0},2).has_value(),
            "Embedded stake must remain targetable for free-hand movement");
    {
        viewer::PhysicsGrab grab(prop,{0,0,0},1000);
        require(!flight.embedded(id),"Grabbing must remove embedding immediately, before another physics step");
    }
    prop.applyImpulse({12,12,0},prop.center());
    world->step(1.f/60);
    pose = items.poses()[0].body;
    const auto velocity = prop.pointState({0,0,0}).velocity;
    require(glm::dot(pose.rotation*glm::vec3(0,0,-1),glm::normalize(velocity)) > .999f,
            "Retrieved stake must fly along its new velocity");
    require(glm::distance(pose.center,pinnedCenter) > .1f,"Released embedding must not constrain a new throw");
    require(items.erase(id),"Could not remove airborne stake");
    world->step(1.f/60); // Deleted body generations must not leave an attachment.
}
void retrieval(bool equip) {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::GroundItems items(world);
    viewer::CollisionWorld collision(floor());
    viewer::PhysicsScene scene(world,collision,{-15,-1,-15},{15,20,15});
    viewer::StakeProjectiles flight(items,world,collision);
    auto inventory = viewer::loadPlayerInventory(root);
    viewer::Weapons weapons(root/"storage/weapons",root/"inventory.toml",true);
    auto duplicateInventory = inventory;
    auto duplicateWeapons = weapons;
    viewer::InventoryDrag drag(inventory);
    require(drag.pickUp("stake"),"Starter stake must fit in inventory");
    require(items.drop(inventory,drag,weapons,{0,.05f,0},{0,0,-1},collision),"Stake must be a transferable equippable item");
    const auto id = items.poses()[0].entry.id;
    const auto prop = *items.prop(id);
    prop.applyImpulse({0,-30,0},prop.center());
    for (int i=0; i<20 && !flight.embedded(id); ++i) { items.prepare(scene); world->step(1.f/60); }
    require(flight.embedded(id),"Retrieval fixture must be embedded");
    const auto center = prop.center();
    viewer::Camera camera; camera.setPosition(center+glm::vec3(0,0,1)); camera.setOrientation(-90,0);
    require(!items.pickUp(duplicateWeapons,duplicateInventory,camera,collision) && flight.embedded(id),
            "A rejected duplicate pickup must leave the stake embedded");
    const bool ok = equip ? items.equip(weapons,inventory,camera,collision,{}) : items.pickUp(weapons,inventory,camera,collision);
    require(ok,"G/H retrieval must acquire an embedded stake");
    require(inventory.owns("stake") && weapons.state(viewer::stakeSlot).owned,"Retrieval must restore stake ownership");
    if (equip) require(weapons.slot() == viewer::stakeSlot,"H must equip the retrieved stake");
    else require(weapons.slot() == 0 && inventory.placements().at("stake").storage == "backpack/tactical_backpack/main",
                 "G must store the stake in backpack and preserve selected equipment");
    world->step(1.f/60);
    require(!flight.embedded(id),"Retrieving a stake must remove its attachment");
}
void zombieAttachment() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::GroundItems items(world);
    viewer::CollisionWorld collision(floor());
    viewer::PhysicsScene scene(world,collision,{-15,-1,-15},{15,20,15});
    auto zombies = sword_fixture::population(); zombies.attachPhysics(world);
    viewer::StakeProjectiles flight(items,world,collision,&zombies);
    const auto id = items.add(stake(),0,viewer::WeaponItem{viewer::stakeSlot,0,0},{0,1.3f,.4f},{0,0,-30});
    for (int i=0; i<10 && !flight.embedded(id); ++i) { items.prepare(scene); world->step(1.f/60); }
    require(flight.embedded(id),"Fast point must embed in the zombie's posed body");
    const auto before = items.prop(id)->center();
    auto& zombie = zombies.instances[0];
    const glm::vec3 motion(.7f,0,0);
    zombie.transform = glm::translate(glm::mat4(1),motion)*zombie.transform;
    zombie.restRoot = glm::translate(glm::mat4(1),motion)*zombie.restRoot;
    zombie.restTransform = glm::translate(glm::mat4(1),motion)*zombie.restTransform;
    world->step(1.f/60);
    require(glm::distance(items.prop(id)->center(),before+motion) < .01f,"Embedded stake must follow its moving zombie");
    require(zombies.knockDown(0,{0,0,0},0),"Could not create physical zombie pose");
    zombies.prepareScenePhysics(scene);
    world->step(1.f/60);
    require(flight.embedded(id),"Zombie ragdoll handoff must retain the stake attachment");
    const auto ragdollCenter = items.prop(id)->center();
    zombies.translateRagdoll(0,{.5f,.1f,0});
    world->step(1.f/60);
    require(glm::distance(items.prop(id)->center(),ragdollCenter) > .3f,"Stake must follow the ragdoll bone after handoff");
    {
        viewer::PhysicsGrab grab(*items.prop(id),{0,0,0},1000);
        require(!flight.embedded(id),"A hand must be able to pull a stake from a zombie");
    }
}
void occlusion() {
    auto model = floor();
    model.draws.push_back({0,glm::translate(glm::mat4(1),{0,1,-.5f}) * glm::scale(glm::mat4(1),{4,3,.1f}),"wall"});
    viewer::CollisionWorld collision(model);
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::GroundItems items(world);
    auto zombies = sword_fixture::population(); zombies.attachPhysics(world);
    viewer::StakeProjectiles flight(items,world,collision,&zombies);
    const auto id = items.add(stake(),0,viewer::WeaponItem{viewer::stakeSlot,0,0},{0,1.3f,.4f},{0,0,-90});
    world->step(1.f/60);
    require(!flight.embedded(id),"Tip sweep must not embed into a zombie behind a wall");
}
void fatalHitAndCorpse() {
    for (const bool alreadyDead : {false,true}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::GroundItems items(world);
        viewer::CollisionWorld collision(floor());
        viewer::PhysicsScene scene(world,collision,{-15,-1,-15},{15,20,15});
        auto zombies = sword_fixture::population(); zombies.attachPhysics(world);
        sword_fixture::weaken(zombies);
        if (alreadyDead) require(zombies.shoot({0,1.3f,0},{0,0,-1},1,viewer::ShotCaliber::high),"Could not kill corpse fixture");
        viewer::StakeProjectiles flight(items,world,collision,&zombies);
        const auto id = items.add(stake(),0,viewer::WeaponItem{viewer::stakeSlot,0,0},{0,1.3f,.4f},{0,0,-30});
        for (int i=0; i<10 && !flight.embedded(id); ++i) {
            items.prepare(scene); zombies.prepareScenePhysics(scene); world->step(1.f/60);
        }
        require(flight.embedded(id) && zombies.instances[0].ragdoll,
                "Both a fatal tip impact and a hit on an existing corpse must retain an attachment");
        const auto center = items.prop(id)->center();
        zombies.translateRagdoll(0,{.5f,0,0}); world->step(1.f/60);
        require(glm::distance(items.prop(id)->center(),center) > .3f,"Corpse attachment must follow its physical bone");
        zombies.removeRagdoll(0); world->step(1.f/60);
        require(!flight.embedded(id),"Retiring a zombie must release its attached stake");
    }
}
}
int main() {
    try {
        flightAndGround(); retrieval(false); retrieval(true); zombieAttachment(); occlusion(); fatalHitAndCorpse();
        std::cout << "Stake checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
