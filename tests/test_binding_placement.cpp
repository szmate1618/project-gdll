#include "binding_placement.hpp"
#include "box_layer.hpp"
#include "camera.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "ground_items.hpp"
#include "inventory/item_config.hpp"
#include "physics_grab.hpp"
#include "physics_world.hpp"
#include "player_interaction.hpp"
#include <box3d/box3d.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const std::string& message) { if (!value) throw std::runtime_error(message); }
const std::filesystem::path root(VIEWER_ITEM_ROOT);
b3BodyId bodyOf(const std::shared_ptr<viewer::PhysicsWorld>& world,const viewer::PhysicsProp& prop) {
    struct Query { const viewer::PhysicsProp& prop; std::shared_ptr<viewer::PhysicsWorld> world; b3BodyId body{}; } query{prop,world,{}};
    const auto center=prop.center();
    b3World_OverlapAABB(world->id(),{{center.x-1,center.y-1,center.z-1},{center.x+1,center.y+1,center.z+1}},
        b3DefaultQueryFilter(),[](b3ShapeId shape,void* context) {
            auto& query=*static_cast<Query*>(context);
            const auto body=b3Shape_GetBody(shape);
            if (viewer::PhysicsProp(query.world,body)==query.prop) { query.body=body; return false; }
            return true;
        },&query);
    require(b3Body_IsValid(query.body),"Could not resolve binding fixture body");
    return query.body;
}
struct Fixture {
    std::shared_ptr<viewer::PhysicsWorld> world=std::make_shared<viewer::PhysicsWorld>(root/"storage");
    viewer::BoxLayer boxes{world};
    viewer::GroundItems items{world};
    viewer::Camera camera;
    Fixture() { camera.setPosition({0,1,.7f}); camera.setOrientation(-90,0); }
    viewer::PhysicsProp tool(const char* name="duct_tape") {
        const auto prop=*items.prop(items.add(viewer::loadItem(root/"storage/items",name),1000,{}, {0,1,0}));
        world->parkCraftingProp(bodyOf(world,prop));
        return prop;
    }
    std::size_t box(glm::vec3 center,glm::vec3 size=glm::vec3(.1f)) {
        viewer::BoxSpawnMotion motion; motion.mass=1; motion.friction=.7f;
        const auto index=boxes.addCompound(center,{{viewer::PropColliderShape::Box,{},size*.5f,0}},motion);
        b3Body_SetGravityScale(bodyOf(world,boxes.prop(index)),0);
        return index;
    }
    void step(int count=1) { for (int frame=0;frame<count;++frame) world->step(1.f/60); }
};
void pullsRespectContactsAndTieOnRelease() {
    Fixture f;
    const auto a=f.box({-.09f,1,0}), b=f.box({.09f,1,0});
    const auto tool=f.tool(); auto& binding=f.items.bindings();
    require(binding.isBinding(tool),"Inline bind tags must enable the tool");
    binding.update(tool,f.camera);
    require(!f.items.bindingLines().empty() && binding.jointCount()==0,
        "Dragging must show intersection lines and defer completed ties until release");
    f.step(90);
    const float distance=glm::distance(f.boxes.pose(a).center,f.boxes.pose(b).center);
    require(distance<.14f && distance>.095f,
        "Binding springs must tighten without overlap; reached separation "+std::to_string(distance));
    require(binding.finish(tool) && f.items.bindingLines().empty() && binding.jointCount()==5,
        "Release must replace temporary pulling forces with five persistent rope constraints and clear preview lines");
    const auto bodyA=bodyOf(f.world,f.boxes.prop(a));
    std::array<b3JointId,8> joints{};
    const int count=b3Body_GetJoints(bodyA,joints.data(),static_cast<int>(joints.size()));
    require(count==5,"Binding must create one tie for each intersected plane");
    for (int i=0;i<count;++i)
        require(b3Joint_GetType(joints[static_cast<std::size_t>(i)])==b3_distanceJoint &&
                b3Joint_GetCollideConnected(joints[static_cast<std::size_t>(i)]),
            "Completed ties must retain physical contacts between connected objects");
    f.boxes.applyImpulse(a,{-1,0,0},f.boxes.pose(a).center);
    f.boxes.applyImpulse(b,{1,0,0},f.boxes.pose(b).center);
    f.step(30);
    require(glm::distance(f.boxes.pose(a).center,f.boxes.pose(b).center)<distance+.025f,
        "Released tied objects must resist separation");
    f.items.erase(*f.items.idOf(tool));
    require(binding.jointCount()==5,"Storing/removing the binding tool must preserve completed ties");
    f.boxes.remove(b); f.step();
    require(binding.jointCount()==0,"Deleting a bound body must clean invalid tie handles");
}
void fixedGeometryBlocksPullsAndCancellationClears() {
    Fixture f;
    const auto left=f.box({-.1f,1,0}), right=f.box({.1f,1,0});
    const auto wall=f.box({0,1,0},{.04f,.24f,.12f});
    b3Body_SetType(bodyOf(f.world,f.boxes.prop(wall)),b3_staticBody);
    const auto tool=f.tool("wire_spool"); auto& binding=f.items.bindings();
    binding.update(tool,f.camera); f.step(60);
    require(f.boxes.pose(left).center.x<-.064f && f.boxes.pose(right).center.x>.064f,
        "A static obstacle must stop tightening without teleporting objects through it");
    binding.cancel(tool);
    require(binding.jointCount()==0 && f.items.bindingLines().empty() && b3World_GetCounters(f.world->id()).jointCount==0,
        "Cancelling must clear temporary pulls and lines without committing a tie");
    const auto plain=*f.items.prop(f.items.add(viewer::loadItem(root/"storage/items","broom"),0,{}, {0,1,0}));
    require(!binding.isBinding(plain),"Crafting without a bind tag must not activate binding");
}
void parkedTargetsAndSharedHand() {
    Fixture f;
    const auto a=f.box({-.09f,1,0}), b=f.box({.09f,1,0});
    const auto bodyA=bodyOf(f.world,f.boxes.prop(a)), bodyB=bodyOf(f.world,f.boxes.prop(b));
    b3Body_SetGravityScale(bodyA,1); b3Body_SetGravityScale(bodyB,1);
    f.world->parkCraftingProp(bodyA); f.world->parkCraftingProp(bodyB);
    const auto tool=f.tool();
    viewer::PlayerInteraction hand;
    viewer::CollisionWorld collision(viewer::Model{});
    viewer::DoorLayer doors({},f.world);
    hand.setCraftingMode(true,&f.items);
    require(hand.grip(f.camera,f.boxes,tool),"Could not begin shared binding grip");
    hand.update(f.camera,1.f/60,doors,f.boxes,collision,1);
    f.step(30);
    require(!f.items.bindingLines().empty() && !f.world->isCraftingHeld(bodyA) &&
            std::abs(f.boxes.pose(a).center.y-1)<.005f,
        "Suspended targets must regain collision response during tightening without falling");
    hand.endCraftingDrag();
    require(f.items.bindings().jointCount()==5 && f.world->isCraftingHeld(bodyA) && f.world->isCraftingHeld(bodyB),
        "A shared hand release must commit the tie and preserve existing suspended placement");
    hand.cancel();
    {
        viewer::PhysicsGrab ordinary(f.boxes.prop(a),{},180);
        require(!f.world->isCraftingHeld(bodyA) && !f.world->isCraftingHeld(bodyB),
            "An ordinary regrab must release suspended poses throughout the tied bundle");
    }
}
void regrabDuringPullPreservesGravity() {
    Fixture f;
    const auto a=f.box({-.09f,1,0}); f.box({.09f,1,0});
    const auto body=bodyOf(f.world,f.boxes.prop(a));
    b3Body_SetGravityScale(body,1); f.world->parkCraftingProp(body);
    const auto tool=f.tool(); auto& binding=f.items.bindings();
    binding.update(tool,f.camera);
    require(b3Body_GetGravityScale(body)==0 && b3Body_GetType(body)==b3_dynamicBody,
        "The live preview must temporarily suspend gravity while allowing contacts");
    {
        viewer::PhysicsGrab grab(f.boxes.prop(a),{},180);
        grab.setCraftingMode(true);
        binding.update(tool,f.camera);
    }
    require(b3Body_GetType(body)==b3_dynamicBody && b3Body_GetGravityScale(body)==1,
        "A hand acquired during binding must restore the original gravity on drop");
}
void separatePlanesBindTheirOwnTargets() {
    Fixture f;
    f.box({0,1,.02f},{.06f,.06f,.004f});
    f.box({-.07f,1,-.02f},{.06f,.06f,.004f});
    f.box({.07f,1,-.02f},{.06f,.06f,.004f});
    const auto tool=f.tool(); auto& binding=f.items.bindings();
    binding.update(tool,f.camera);
    require(binding.finish(tool) && binding.jointCount()==1,
        "Targets sharing the fifth plane must bind even when the first target intersects only the first plane");
}
}
int main() {
    try {
        pullsRespectContactsAndTieOnRelease();
        fixedGeometryBlocksPullsAndCancellationClears();
        parkedTargetsAndSharedHand();
        regrabDuringPullPreservesGravity();
        separatePlanesBindTheirOwnTargets();
        std::cout<<"Binding placement checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
