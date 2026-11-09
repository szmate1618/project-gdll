#pragma once
#include "canopy.hpp"
#include "ground_items.hpp"
#include "stake_projectiles.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include "inventory/item_config.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace canopy_fixture {
inline viewer::Model floor() {
    auto model=viewer::BoxLayer::unitCube();
    model.materials[0].baseColor={.29f,.34f,.24f,1};
    model.draws.push_back({0,glm::scale(glm::mat4(1),{12,.1f,12}),"floor"});
    model.boundsMin={-6,-.05f,-6}; model.boundsMax={6,.05f,6};
    return model;
}
struct Shelter {
    std::filesystem::path root{VIEWER_ITEM_ROOT};
    viewer::Model model=floor();
    std::shared_ptr<viewer::PhysicsWorld> world=std::make_shared<viewer::PhysicsWorld>();
    viewer::GroundItems items{world};
    viewer::CollisionWorld collision{model};
    viewer::PhysicsScene scene{world,collision,model.boundsMin,{6,10,6}};
    viewer::StakeProjectiles stakes{items,world,collision};
    std::uint64_t first=0, second=0, cloth=0;
    void step(int count) {
        for (int n=0; n<count; ++n) { items.prepare(scene); world->step(1.f/60); }
    }
    void plant() {
        const auto orientation=glm::angleAxis(glm::radians(-90.f),glm::vec3(1,0,0));
        first=items.add(viewer::loadItem(root/"storage/weapons","stake"),0,
                        viewer::WeaponItem{viewer::stakeSlot,0,0},{-.25f,2,-.75f},{0,-10,0},orientation);
        second=items.add(viewer::loadItem(root/"storage/items","support_stake"),0,{},
                         {-.25f,2,.75f},{0,-10,0},orientation);
        step(30);
        if (!stakes.embedded(first) || !stakes.embedded(second)) throw std::runtime_error("Both support stakes must plant");
    }
    void drape() {
        cloth=items.add(viewer::loadItem(root/"storage/items","canopy"),0,{}, {0,1.8f,0});
        step(600);
    }
    viewer::GroundItemPose canopy() const {
        for (const auto& pose : items.poses()) if (pose.entry.id==cloth) return pose;
        throw std::runtime_error("Canopy disappeared");
    }
};
}
