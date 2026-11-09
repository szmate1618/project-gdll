#include "canopy_fixture.hpp"
#include "physics_grab.hpp"
#include "object_interaction.hpp"
#include "inventory/inventory_config.hpp"
#include "camera.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
glm::vec3 vector(b3Vec3 v) { return {v.x,v.y,v.z}; }
void topologyAndLifetime() {
    const std::filesystem::path root(VIEWER_ITEM_ROOT);
    auto world=std::make_shared<viewer::PhysicsWorld>();
    viewer::GroundItems items(world);
    const auto id=items.add(viewer::loadItem(root/"storage/items","canopy"),0,{}, {0,2,0});
    const auto poses=items.poses().at(0).panels;
    const auto counters=b3World_GetCounters(world->id());
    require(poses.size()==36 && counters.bodyCount==36 && counters.jointCount==60,
            "Canopy must contain exactly 36 rigid plates and 60 shared-edge hinges");
    glm::vec3 minimum(100),maximum(-100);
    std::map<std::uint64_t,int> panels;
    std::vector<b3BodyId> bodies;
    auto filter=b3DefaultQueryFilter(); filter.maskBits=viewer::canopyBodyCategory;
    for (int p=0; p<36; ++p) {
        const auto& pose=poses[p];
        minimum=glm::min(minimum,pose.center-pose.halfExtents);
        maximum=glm::max(maximum,pose.center+pose.halfExtents);
        const auto ray=b3World_CastRayClosest(world->id(),{pose.center.x,3,pose.center.z},{0,-2,0},filter);
        require(ray.hit,"Each plate must have its own collider");
        const auto body=b3Shape_GetBody(ray.shapeId);
        require(panels.emplace(b3StoreBodyId(body),p).second,"Two plate positions share one rigid body");
        bodies.push_back(body);
    }
    require(std::abs(maximum.x-minimum.x-3)<1e-6f && std::abs(maximum.z-minimum.z-3)<1e-6f,
            "Unfolded canopy must measure three metres on both sides");
    for (int p=0; p<36; ++p) {
        std::array<b3JointId,4> joints{};
        const auto count=b3Body_GetJoints(bodies[p],joints.data(),4);
        const int expected=4-(p%6==0)-(p%6==5)-(p/6==0)-(p/6==5);
        require(count==expected,"Plate must connect only to its side neighbours");
        for (int j=0; j<count; ++j) {
            const auto a=b3Joint_GetBodyA(joints[j]),b=b3Joint_GetBodyB(joints[j]);
            const auto ia=panels.at(b3StoreBodyId(a)),ib=panels.at(b3StoreBodyId(b));
            require(std::abs(ia%6-ib%6)+std::abs(ia/6-ib/6)==1,"A diagonal/corner hinge was created");
            require(b3Joint_GetType(joints[j])==b3_revoluteJoint && b3RevoluteJoint_IsSpringEnabled(joints[j]),
                    "Shared edges must use spring revolute hinges");
            const auto pa=vector(b3Body_GetWorldPoint(a,b3Joint_GetLocalFrameA(joints[j]).p));
            const auto pb=vector(b3Body_GetWorldPoint(b,b3Joint_GetLocalFrameB(joints[j]).p));
            require(glm::distance(pa,pb)<1e-6f,"Hinge anchors must meet at a shared side");
        }
    }
    const auto hit=viewer::raycastPhysicsProp(world,{poses.front().center.x,3,poses.front().center.z},{0,-1,0},2);
    require(hit && items.idOf(hit->prop)==id,"Any plate must resolve to the owning inventory item");
    {
        viewer::PhysicsGrab grip(hit->prop,{0,0,0},180);
        require(items.erase(id) && !grip.valid(),"Deleting a canopy must invalidate grips on peripheral plates");
    }
    require(b3World_GetCounters(world->id()).bodyCount==0 && b3World_GetCounters(world->id()).jointCount==0,
            "Removing a canopy leaked rigid bodies or hinges");
}
void shelterAndHandling() {
    canopy_fixture::Shelter fixture;
    fixture.plant();
    const auto first=fixture.items.prop(fixture.first)->center(),second=fixture.items.prop(fixture.second)->center();
    fixture.drape();
    const auto pose=fixture.canopy();
    float minimum=100,maximum=-100;
    for (const auto& panel : pose.panels) {
        require(std::isfinite(panel.center.y) && std::abs(glm::length(panel.rotation)-1)<.001f,
                "Draped panels must retain finite rigid poses");
        minimum=std::min(minimum,panel.center.y); maximum=std::max(maximum,panel.center.y);
    }
    std::cout << "Draped canopy height: " << minimum << ".." << maximum << '\n';
    require(maximum>1.15f && minimum<1.0f,"Canopy must remain supported by two planted stakes and drape down its sides");
    require(glm::distance(first,fixture.items.prop(fixture.first)->center())<.02f &&
            glm::distance(second,fixture.items.prop(fixture.second)->center())<.02f,
            "Canopy weight must not dislodge planted stakes");
    viewer::BoxLayer boxes(fixture.world);
    const auto corner=pose.panels.front().center;
    viewer::Camera camera; camera.setPosition(corner+glm::vec3(0,.7f,0)); camera.setOrientation(-90,-89);
    viewer::ObjectInteraction hand;
    require(hand.press(camera,boxes,fixture.collision) && hand.heldProp() &&
            fixture.items.idOf(*hand.heldProp())==fixture.cloth,"Free-hand mode must grab a peripheral canopy plate");
    hand.moveMouse(0,200);
    for (int i=0; i<90; ++i) { hand.update(camera,1.f/60); fixture.step(1); }
    require(hand.heldProp() && hand.heldProp()->center().y>corner.y+.3f,"Free-hand dragging must lift the canopy");
    hand.cancel();
    auto inventory=viewer::loadPlayerInventory(fixture.root);
    viewer::Weapons weapons(fixture.root/"storage/weapons",fixture.root/"inventory.toml",true);
    viewer::InventoryDrag discard(inventory);
    require(discard.pickUp("canopy") && discard.release(),"Could not free the stored canopy slot");
    const auto target=fixture.canopy().panels.front().center;
    camera.setPosition(target+glm::vec3(0,.7f,0)); camera.setOrientation(-90,-89);
    require(fixture.items.pickUp(weapons,inventory,camera,fixture.collision),"G pickup must retrieve an articulated canopy through any panel");
    require(inventory.owns("canopy") && fixture.items.count()==2,"Retrieval must remove all plates while preserving both stakes");
}
}
int main() {
    try { topologyAndLifetime(); shelterAndHandling(); std::cout<<"Canopy checks passed\n"; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
