#include "zombie_layer.hpp"
#include "navmesh_fixture.hpp"
#include "zombie_crowd.hpp"
#include <limits>
#include <algorithm>
#include "zombie_navigation.hpp"
#include "zombie_update_profile.hpp"
#include "simulation_clock.hpp"
#include "box_layer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void quad(viewer::Model& model,const char* name,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d) {
    viewer::Primitive primitive;
    for (const auto p:{a,b,c,d}) { viewer::Vertex v;v.position=p;primitive.vertices.push_back(v); }
    primitive.indices={0,1,2,0,2,3};
    model.draws.push_back({model.primitives.size(),glm::mat4(1),name});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model terrain(float grade=0,float y=0) {
    viewer::Model model;
    quad(model,"terrain",{-500,y-500*grade,-500},{-500,y-500*grade,500},
         {500,y+500*grade,500},{500,y+500*grade,-500});
    return model;
}
viewer::ZombieLayer traveler(glm::vec3 start,glm::vec3 goal) {
    using namespace viewer::navmesh_path;
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();layer.instances.emplace_back();
    auto& instance=layer.instances[0];
    instance.transform=instance.restTransform=instance.restRoot=glm::translate(glm::mat4(1),start);
    layer.rebuildSpatialIndex();
    auto mesh=Mesh({{-500,-500},{500,-500},{500,500},{-500,500}},{{0,4}},{{0,noPolygon},{1,noPolygon},{2,noPolygon},{3,noPolygon}});
    layer.enableNavigation(std::make_unique<viewer::ZombieNavigation>(std::move(mesh),1));
    layer.setScenarioDestinations({goal});
    return layer;
}
constexpr float dt=1.f/60;
const viewer::ZombiePlayer far{{10000,0,10000}};
void advance(viewer::ZombieLayer& layer,const viewer::CollisionWorld& world,const viewer::ZombiePlayer& player=far) {
    viewer::SimulationClock::instance().advance(dt);
    layer.updateBehavior(dt,player,world);
}
void modesAndGround() {
    for (float grade:{0.f,.03f}) for(bool optimized:{false,true}) {
        const viewer::CollisionWorld world(terrain(grade));
        auto layer=traveler({-10,-10*grade,-10},{100,100*grade,-10});
        layer.setPerceptionEnabled(false);layer.setDistantScenarioTravelEnabled(optimized);
        viewer::zombie_profile::Profile profile;
        for(int frame=0;frame<120;++frame) {
            const auto before=glm::vec3(layer.instances[0].restRoot[3]);
            { viewer::zombie_profile::Capture capture(&profile);advance(layer,world); }
            const auto feet=glm::vec3(layer.instances[0].restRoot[3]);
            require(feet.x>before.x && feet.x-before.x<=1.2f*dt+.0001f,"Traveler motion paused or exceeded the speed bound");
            require(std::abs(feet.y-grade*feet.x)<.00005f,"Smooth traveler feet left the rendered ground");
            require(layer.navigation()->segmentClear(before,feet),"Traveler step escaped navmesh coverage");
        }
        require(std::abs(layer.instances[0].restRoot[3].x-(-10+2.4f))<.002f,"Traveler lost elapsed simulation time");
        const bool low=optimized;
        require(low?(profile.travelerPlans>=19 && profile.travelerPlans<=22):profile.travelerPlans==0,
                "Traveler planning cadence differs from 10 Hz");
        require(layer.navigation()->stats().steeringCalls<=(low?22u:120u),"Distant planning still steers every frame");
        require(layer.navigation()->stats().requests==1,"Traveler requested a runtime path");
    }
}
void profileSeparatesTravelAndPursuit() {
    const viewer::CollisionWorld world(terrain());
    const viewer::ZombiePlayer player{{0,0,20}};
    for (const bool perception : {false,true}) {
        auto control=traveler({-10,0,-10},{100,0,-10});
        auto measured=traveler({-10,0,-10},{100,0,-10});
        control.setPerceptionEnabled(perception);measured.setPerceptionEnabled(perception);
        control.setPerceptionStaggering(false);measured.setPerceptionStaggering(false);
        viewer::zombie_profile::Profile profile;
        for (int frame=0;frame<120;++frame) {
            viewer::SimulationClock::instance().advance(dt);
            control.updateBehavior(dt,player,world);
            { viewer::zombie_profile::Capture capture(&profile);measured.updateBehavior(dt,player,world); }
            const auto& a=control.instances[0];const auto& b=measured.instances[0];
            require(a.restRoot==b.restRoot && a.walkSeconds==b.walkSeconds && a.walking==b.walking && a.running==b.running,
                    "Enabling profiling changed authoritative movement or animation");
        }
        require(profile.steps==120,"Profile lost simulation steps");
        require(profile.behaviorStates[3]==(perception?120u:0u),
                "Scenario travel was counted as chasing, or pursuit was omitted");
        require(profile.chaseStarts==(perception?1u:0u),"Chase-start profiling counted ongoing pursuit as activation");
        require((measured.scenarioDestination(0)!=nullptr)==!perception,
                "Profile fixture failed to distinguish scenario travel from a perception handoff");
    }
}
void allPlayersAndTransitions() {
    const viewer::CollisionWorld world(terrain());
    auto layer=traveler({-10,0,-10},{100,0,-10});layer.setPerceptionEnabled(false);
    for(int i=0;i<10;++i) advance(layer,world);
    for(int i=0;i<10;++i) {
        const auto feet=glm::vec3(layer.instances[0].restRoot[3]);
        const auto calls=layer.navigation()->stats().steeringCalls;
        layer.updateBehaviorForPlayers(dt,{far,viewer::ZombiePlayer{feet+glm::vec3(0,0,100)}},world,0);
        require(layer.navigation()->stats().steeringCalls==calls+1,"A second nearby player did not restore full updates");
        require(glm::distance(feet,glm::vec3(layer.instances[0].restRoot[3]))<=1.2f*dt+.0001f,"Near transition caught up a future position");
    }
    for(int i=0;i<12;++i) advance(layer,world);
    const auto calls=layer.navigation()->stats().steeringCalls;
    layer.setDistantScenarioTravelEnabled(false);
    for(int i=0;i<6;++i) advance(layer,world);
    require(layer.navigation()->stats().steeringCalls==calls+6,"Comparison mode retained a pending coarse segment");

    auto visible=traveler({-10,0,-10},{100,0,-10});
    for(int i=0;i<60;++i) advance(visible,world);
    const auto before=glm::vec3(visible.instances[0].restRoot[3]);
    const auto forward=glm::vec3(visible.instances[0].restRoot[2]);
    advance(visible,world,viewer::ZombiePlayer{before+forward*10.f});
    require(!visible.scenarioDestination(0) && !visible.navigation()->pathFor(0),"Perception failed to abandon a coarse route");
    require(glm::distance(before,glm::vec3(visible.instances[0].restRoot[3]))<=3*dt+.0001f,"Perception advanced both movement systems");
    require(visible.navigation()->stats().requests==1,"Behavior handoff requested a path");
}
void arrivalAndReplacement() {
    viewer::CollisionWorld world(terrain());
    auto layer=traveler({-10,0,-10},{-9.82f,0,-10});layer.setPerceptionEnabled(false);
    for(int i=0;i<40;++i) advance(layer,world);
    require(layer.scenarioFinished(0) && !layer.instances[0].walking,"Coarse traveler did not arrive and stop");
    require(std::abs(layer.instances[0].restRoot[3].x+9.82f)<.0001f && !layer.navigation()->pathFor(0),"Arrival overshot or retained a path");
    auto moving=traveler({-10,0,-10},{100,0,-10});moving.setPerceptionEnabled(false);
    advance(moving,world);
    world=viewer::CollisionWorld(terrain(0,-.1f));
    advance(moving,world);
    require(std::abs(moving.instances[0].restRoot[3].y+.1f)<.0001f,"Pending traveler endpoint survived world replacement");
}
void representableCorners() {
    viewer::Model model;
    quad(model,"terrain",{-5000,0,-5000},{-5000,0,5000},{5000,0,5000},{5000,0,-5000});
    const viewer::CollisionWorld world(model);
    for(bool tinyCorner:{false,true}) for(bool distant:{false,true}) {
        // The second corner's Z is just above a representable float. Extending
        // the incoming direction still rounds below its outgoing boundary.
        const double x=tinyCorner?3000.00001:3060.119;
        const double z=tinyCorner?1000.00000001:-2539.732;
        const double scale=tinyCorner?1:1.07;
        auto mesh=tinyCorner?test_navmesh::convexSquares(3,2,{{1,0}}):
            test_navmesh::convexSquares(10,8,{{4,0},{4,1},{4,2},{4,3},{4,4},{4,5}});
        auto vertices=mesh.vertices();
        for(auto& point:vertices) { point.x=x+point.x*scale;point.z=z+point.z*scale; }
        mesh=viewer::navmesh_path::Mesh(std::move(vertices),mesh.polygons(),mesh.edges());
        viewer::ZombieNavigation navigation(std::move(mesh),1);
        glm::vec3 feet(float(x+(tinyCorner?.95:1.5)*scale),0,float(z+(tinyCorner?1:1.5)*scale));
        const glm::vec3 goal(float(x+(tinyCorner?2.5:8.5)*scale),0,float(z+(tinyCorner?.5:1.5)*scale));
        require(navigation.plan(0,feet,goal),"Corner regression failed cold planning");
        viewer::ZombieCrowd crowd;crowd.reset(1);crowd.update(0,feet);
        viewer::WalkingGroundCache ground;viewer::ContactCandidates contacts;viewer::TravelerPlan plan;
        for(int frame=0;frame<1600 && glm::length(feet-goal)>1e-4f;++frame) {
            crowd.beginStep();
            const auto step=viewer::moveScenarioTraveler(0,feet,dt,world,navigation,crowd,ground,contacts,plan,distant);
            require(navigation.segmentClear(feet,step.movement.feet),"Corner recovery cut across blocked mesh");
            const float tolerance=2*std::numeric_limits<float>::epsilon()*std::max({1.f,std::abs(feet.x),std::abs(feet.z)});
            require(glm::length(glm::vec2(step.movement.feet.x-feet.x,step.movement.feet.z-feet.z))<=1.2f*dt+tolerance,
                    "Corner recovery teleported instead of walking");
            navigation.recordMovement(0,feet,step.movement.feet,dt);feet=step.movement.feet;crowd.update(0,feet);
        }
        if(glm::length(feet-goal)>1e-4f) {
            std::cerr<<"Corner case tiny="<<tinyCorner<<" distant="<<distant<<" feet="<<feet.x<<','<<feet.z<<" goal="<<goal.x<<','<<goal.z<<'\n';
            throw std::runtime_error("Representable corner stalled before its destination");
        }
        require(navigation.stats().requests==1,"Corner recovery searched for another path");
    }
}
void obstacleFallback() {
    auto model=terrain();
    quad(model,"building_test_walls",{0,0,-20},{0,0,20},{0,3,20},{0,3,-20});
    viewer::CollisionWorld world(model,{{{-2,0,3},.15f,5}});
    viewer::ContactCandidates cache;
    require(!world.simpleTravelerClear({-1,0,0},{1,0,0},.3f,1.8f,cache),"Simple wall sweep missed a crossing");
    require(!world.simpleTravelerClear({-3,0,3},{-1,0,3},.3f,1.8f,cache),"Simple trunk sweep missed a crossing");
    require(world.simpleTravelerClear({-3,0,-3},{-2,0,-3},.3f,1.8f,cache),"Clear outdoor segment unnecessarily failed");
    auto layer=traveler({-1,0,0},{10,0,0});layer.setPerceptionEnabled(false);
    for(int i=0;i<120;++i) {
        advance(layer,world);
        require(layer.instances[0].restRoot[3].x<=-.349f,"Distant traveler crossed a physical wall");
    }
    const auto physics=std::make_shared<viewer::PhysicsWorld>();viewer::BoxLayer boxes(physics);
    boxes.add({-2,.5f,-3},{.5f,1,.5f});world.setBoxes(&boxes);
    require(!world.simpleTravelerClear({-3,0,-3},{-1,0,-3},.3f,1.8f,cache),"A newly inserted prop was ignored");
}
}
int main() {
    try {
        viewer::SimulationClock::instance().reset();
        modesAndGround();profileSeparatesTravelAndPursuit();allPlayersAndTransitions();arrivalAndReplacement();representableCorners();obstacleFallback();
        std::cout<<"Scenario traveler tests passed\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
