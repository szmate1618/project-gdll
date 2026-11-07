#include "fire_effects.hpp"
#include "fire_surfaces.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "interior_fixture.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::size_t count(const viewer::FireEffects& fire) {
    std::size_t result = 0;
    for (const auto& p : fire.patches()) result += p.active;
    return result;
}
viewer::Model room() {
    auto model = interior_fixture::town();
    interior_fixture::quad(model,{-3,0,-1},{3,0,-1},{3,4,-1},{-3,4,-1},"wall");
    interior_fixture::bounds(model);
    return model;
}
void coneAndDuration() {
    for (const auto direction : {glm::vec3(0,-1,0),{1,0,0},{0,0,-1}}) {
        const auto rays = viewer::molotovRayDirections(direction,17);
        require(rays.size()==10 && rays==viewer::molotovRayDirections(direction,17),"Splash seed was not repeatable");
        require(rays!=viewer::molotovRayDirections(direction,18),"Different bottles reused the same random cone");
        for (const auto ray : rays) require(std::abs(glm::length(ray)-1)<.0001f &&
            glm::dot(ray,direction)>=std::cos(glm::radians(30.f))-.0001f,"Ray escaped the 60-degree full cone");
    }
    viewer::FireEffects fire;
    std::size_t casts = 0;
    fire.ignite({1,{0,1,0},{0,0,-1},0},100,[&](auto origin,auto direction,float reach) {
        ++casts; require(reach==2,"Molotov rays must reach exactly two metres");
        return std::optional<viewer::FireContact>{{origin+direction*reach,-direction}};
    });
    require(casts==10 && count(fire)==10 && fire.hasEffects(109.999) && !fire.hasEffects(110),
        "A splash must emit ten rays and leave fire for exactly ten seconds");
    viewer::FireEffects miss;
    miss.ignite({2,{0,1,0},{0,0,-1},0},100,[](auto,auto,float)->std::optional<viewer::FireContact>{ return {}; });
    require(count(miss)==0,"Missed rays created fire");
    miss.ignite({3,{0,1,0},{0,0,-1},0},100,[](auto o,auto d,float) {
        return std::optional<viewer::FireContact>{{o+d*2.01f,-d}};
    });
    require(count(miss)==0,"A hit beyond two metres created fire");
}
void surfacesAndPlayback() {
    const auto model = room(); viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    viewer::FireScene scene{collision,physics};
    viewer::FireSurfaceQuery query(scene,{0,1,0});
    const auto wall = query.cast({0,1,0},{0,0,-1},2);
    require(wall && std::abs(wall->point.z+1)<.0001f,"Splash missed a static wall within reach");
    require(!query.cast({0,1,0},{0,0,1},2),"Splash ray hit geometry outside its range");
    boxes.add({0,1,-.5f},{.3f,.3f,.3f});
    const auto prop = query.cast({0,1,0},{0,0,-1},2);
    require(prop && prop->attachment==viewer::FireAttachment::body && prop->point.z>-.7f,
        "Splash did not choose the prop before the wall");
    viewer::FireEffects fire;
    fire.ignite({5,{0,1,0},{0,0,-1},0},100,[&](auto,auto,float) { return prop; });
    auto pose = boxes.pose(0); pose.center.x = 2;
    boxes.setPose(0,pose.center,pose.rotation);
    fire.update(101,scene);
    require(std::abs(fire.patches()[0].contact.point.x-2)<.0001f,"Fire did not follow a moved prop");
    boxes.remove(0); fire.update(102,scene);
    require(!fire.hasEffects(102),"Removed props retained attached fire");
    viewer::FireEffects playback;
    std::vector<viewer::MolotovImpact> events{{9,{0,1,0},{0,0,-1},2}};
    playback.receive(events,20,17.9,100,scene);
    require(count(playback)==0,"Future molotov impact played early");
    playback.receive(events,20,20,100,scene);
    require(count(playback)==10 && playback.patches()[0].born==98,"Late impact restarted a fresh ten seconds");
    playback.receive(events,20,20,105,scene);
    require(count(playback)==10 && playback.patches()[0].born==98,"Repeated impact duplicated or restarted fire");
    playback.receive(events,20,20,108,scene);
    require(!playback.hasEffects(108),"Fire failed to expire during a stalled server clock");
}
}
int main() {
    try { coneAndDuration(); surfacesAndPlayback(); std::cout<<"Molotov cones, surfaces, attachments and fire lifetime passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
