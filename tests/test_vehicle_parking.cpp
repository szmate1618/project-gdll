#include "vehicle_player.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "interior_fixture.hpp"
#include "zombie_movement.hpp"
#include <box3d/box3d.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool sameWheels(const std::array<glm::mat4,4>& a,const std::array<glm::mat4,4>& b) {
    // Matrix -> quaternion -> Box3D -> matrix conversion rounds float rotations.
    for(std::size_t i=0;i<4;++i) for(int column=0;column<4;++column)
        if(glm::length(a[i][column]-b[i][column])>1e-5f) return false;
    return true;
}
viewer::Model ground() {
    viewer::Model model; model.materials.push_back({});
    interior_fixture::quad(model, {-80,0,-80}, {-80,0,80}, {80,0,80}, {80,0,-80}, "terrain");
    interior_fixture::bounds(model); model.boundsMax.y=30;
    return model;
}
void parkedFleet() {
    auto world=std::make_shared<viewer::PhysicsWorld>();
    const auto model=ground(); viewer::CollisionWorld collision(model);
    viewer::PhysicsScene scene(world,collision,model.boundsMin,model.boundsMax);
    viewer::VehicleLayer cars(world);
    for (int i=0;i<1000;++i) cars.add({float(i%40)*5,.68f,float(i/40)*6});
    collision.setVehicles(&cars);
    const auto initial=b3World_GetCounters(world->id());
    require(initial.bodyCount==1000 && initial.shapeCount==2000 && initial.jointCount==0,
            "Parked fleet must contain exactly one body and two boxes per car, with no wheel joints");
    require(cars.simulatedCars().empty(),"Parked fleet entered detailed simulation");
    const auto pose=cars.pose(0); const auto revision=cars.poseRevision(0);
    const auto start=std::chrono::steady_clock::now();
    for (int frame=0;frame<120;++frame) {
        viewer::prepareVehicleScene(cars,scene);
        world->step(1.f/60,[&](float dt) { cars.beforeStep(dt); });
    }
    const auto after=b3World_GetCounters(world->id());
    require(after.bodyCount==initial.bodyCount && after.shapeCount==initial.shapeCount && after.jointCount==0,
            "Parked cars caused terrain physics construction or wheel activation");
    require(cars.poseRevision(0)==revision && cars.pose(0).chassis.center==pose.chassis.center,
            "Parked car fell or invalidated render bounds during physics steps");
    viewer::ContactCandidates cache;
    require(collision.simpleTravelerClear({-3,0,-20},{3,0,-20},.3f,1.8f,cache),
            "Empty traveler corridor could not be certified");
    auto relocated=cars.pose(0);relocated.chassis.center.z-=20;
    for(auto& wheel:relocated.wheels) wheel[3].z-=20;
    cars.setPose(0,relocated,{});
    require(!collision.simpleTravelerClear({-3,0,-20},{3,0,-20},.3f,1.8f,cache),
            "Cached traveler shortcut ignored a newly present parked car");
    cars.setPose(0,pose,{});
    std::vector<viewer::Contact> contacts;
    cars.appendContacts({.83f,.1f,0},.3f,1.7f,0,contacts);
    require(!contacts.empty(),"Walking capsule passed through the parked lower body");
    contacts.clear(); cars.appendContacts({.70f,1.1f,0},.2f,.5f,0,contacts);
    require(!contacts.empty(),"Walking capsule passed through the parked cabin");
    const auto moved=viewer::moveZombieBody({2,0,0},{0,0,0},collision);
    require(moved.feet.x>1.1f && cars.simulatedCars().empty(),
            "Kinematic zombie crossed a parked car or activated its suspension");
    const auto roof=cars.groundAt(0,0,1.1f,2);
    require(roof && std::abs(roof->height-1.56f)<.002f,"Parked cabin is missing from ground queries");
    std::cout<<"1000 parked cars: "<<after.bodyCount<<" bodies, "<<after.shapeCount<<" boxes, "<<after.jointCount
             <<" joints; fixed simulation ms/frame="
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120<<'\n';
}
void transitions() {
    auto world=std::make_shared<viewer::PhysicsWorld>();
    const auto model=ground(); viewer::CollisionWorld collision(model);
    viewer::PhysicsScene scene(world,collision,model.boundsMin,model.boundsMax);
    viewer::VehicleLayer cars(world); cars.add({0,.8f,0});
    const auto initial=cars.pose(0);
    cars.setDriven(0,true);
    auto count=b3World_GetCounters(world->id());
    require(count.bodyCount==5 && count.shapeCount==5 && count.jointCount==4 && cars.simulatedCars().size()==1,
            "Driver did not enable exactly one chassis, four wheels and four suspension joints");
    require(cars.pose(0).chassis.center==initial.chassis.center && sameWheels(cars.pose(0).wheels,initial.wheels),
            "Driver entry changed the car pose");
    const auto tick=[&](int frames) {
        for(int i=0;i<frames;++i) {
            viewer::prepareVehicleScene(cars,scene);
            world->step(1.f/60,[&](float dt) { cars.beforeStep(dt); });
        }
    };
    tick(120); cars.drive(0,1,0,false); tick(60);
    require(cars.velocity(0).z < -3,"Activated parked car could not drive");
    cars.setDriven(0,false);
    tick(1); require(cars.simulating(0),"Driver release froze a moving car");
    tick(360);
    require(!cars.simulating(0) && cars.simulatedCars().empty(),"Stopped unattended car retained wheel physics");
    count=b3World_GetCounters(world->id());
    require(count.jointCount==0 && count.shapeCount>0,"Parking retained suspension joints");
    const auto parked=cars.pose(0); const auto revision=cars.poseRevision(0);
    tick(30);
    require(cars.pose(0).chassis.center==parked.chassis.center && cars.poseRevision(0)==revision,
            "Parked car drifted or changed render revision");
    cars.setDriven(0,true);
    require(sameWheels(cars.pose(0).wheels,parked.wheels),"Re-entry discarded the settled wheel poses");
    cars.drive(0,-1,0,false); tick(90);
    require(cars.velocity(0).z>2,"Parked car failed to drive after re-entry");
    cars.setDriven(0,false); tick(360);
    require(!cars.simulating(0),"A second parking transition failed");
}
void grenadeCategoryContact() {
    auto world=std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(world);cars.add({0,.68f,0});
    auto body=b3DefaultBodyDef();body.type=b3_dynamicBody;body.position={-1.6f,.7f,0};body.linearVelocity={6,0,0};
    const auto grenade=b3CreateBody(world->id(),&body);
    auto shape=b3DefaultShapeDef();shape.filter.categoryBits=viewer::grenadeBodyCategory;
    const b3Sphere sphere{{0,0,0},.15f};b3CreateSphereShape(grenade,&shape,&sphere);
    require(!cars.carForBody(grenade),"Grenade was identified as a vehicle body");
    for(int i=0;i<10;++i) world->step(1.f/60);
    require(b3Body_GetPosition(grenade).x<-.95f && !cars.simulating(0),
            "Grenade passed through a parked car or enabled its suspension");
    b3DestroyBody(grenade);
}
void replicationAndAir() {
    auto world=std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(world), other(world);
    cars.add({0,20,0}); other.add({10,.68f,0});
    auto pose=cars.pose(0); pose.chassis.center.x=3;
    for(auto& wheel:pose.wheels) wheel[3].x+=3;
    cars.setPose(0,pose,{0,0,-5});
    const auto count=b3World_GetCounters(world->id());
    require(count.bodyCount==2 && count.shapeCount==4 && count.jointCount==0 && !cars.simulating(0),
            "Replicated car pose allocated local suspension physics");
    require(cars.pose(0).wheels==pose.wheels && cars.velocity(0).z==-5,"Replication lost wheel poses or velocity");
    auto filter=b3DefaultQueryFilter(); filter.maskBits=viewer::vehicleBodyCategory;
    struct Query { viewer::VehicleLayer* cars; bool found=false; } query{&cars};
    b3World_OverlapAABB(world->id(),{{8,0,-2},{12,2,2}},filter,[](b3ShapeId shape,void* context) {
        auto& q=*static_cast<Query*>(context);
        q.found=true;
        require(!q.cars->carForBody(b3Shape_GetBody(shape)),"Body ownership crossed vehicle layers");
        return true;
    },&query);
    require(query.found,"Cross-layer ownership fixture did not find the other car");
    cars.setDriven(0,true); cars.setDriven(0,false);
    for(int i=0;i<10;++i) world->step(1.f/60,[&](float dt) {cars.beforeStep(dt);});
    require(cars.simulating(0) && cars.pose(0).chassis.center.y<20,"An airborne unattended car froze before landing");
}
void sweptActivation() {
    for (const glm::vec3 velocity : {glm::vec3(0,0,-90), {0,0,90}, {90,0,0}}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::VehicleLayer cars(world);
        cars.add({0,.8f,0});
        const auto direction = glm::normalize(velocity);
        const float separation = velocity.x != 0 ? 3.2f : 4.5f;
        cars.add(glm::vec3(0,.8f,0) + direction * separation);
        cars.add(glm::vec3(0,.8f,0) - direction * separation);
        cars.add({10,.8f,10});
        cars.add({0,8.8f,0});
        cars.setDriven(0,true);
        const auto initial = cars.pose(1);
        cars.setPose(0,cars.pose(0),velocity);
        cars.beforeStep(viewer::PhysicsWorld::fixedStepSeconds);
        require(cars.simulating(1),"Travel bounds did not wake a parked car before forward, reverse or lateral impact");
        require(!cars.simulating(2) && !cars.simulating(3) && !cars.simulating(4),
                "Travel activation woke a car behind, distant from or above the moving vehicle");
        require(cars.pose(1).chassis.center == initial.chassis.center && sameWheels(cars.pose(1).wheels,initial.wheels),
                "Pre-impact activation changed the parked car pose");
        require(cars.simulatedCars().size() == 2 && b3World_GetCounters(world->id()).jointCount == 8,
                "Multiple parked shapes activated the same suspension more than once");
    }
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(world);
    cars.add({0,.8f,0}); cars.add({2.2f,.8f,0});
    cars.setDriven(0,true);
    cars.beforeStep(viewer::PhysicsWorld::fixedStepSeconds);
    require(!cars.simulating(1),"A stationary car woke an adjacent parked car");
    // The chassis owns four suspension joints; wheels each own only one.
    auto filter = b3DefaultQueryFilter(); filter.maskBits = viewer::vehicleBodyCategory;
    b3World_OverlapAABB(world->id(),{{-1,0,-2},{1,2,2}},filter,[](b3ShapeId shape,void*) {
        const auto body = b3Shape_GetBody(shape);
        if (b3Body_GetJointCount(body) == 4) b3Body_SetAngularVelocity(body,{0,18,0});
        return true;
    },nullptr);
    cars.beforeStep(viewer::PhysicsWorld::fixedStepSeconds);
    require(cars.simulating(1),"A rotating chassis did not wake a car in its angular sweep");
}
void pileupAndSettling() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    const auto model = ground(); viewer::CollisionWorld collision(model);
    viewer::PhysicsScene scene(world,collision,model.boundsMin,model.boundsMax);
    viewer::VehicleLayer cars(world);
    for (int i=0;i<3;++i) cars.add({0,.8f,-3.9f*i});
    cars.add({20,.8f,0}); collision.setVehicles(&cars);
    cars.applyImpulse(0,{0,0,-13200},cars.pose(0).chassis.center);
    bool secondMoved = false, thirdMoved = false;
    for (int frame=0;frame<600;++frame) {
        world->step(1.f/60,[&](float dt) {
            cars.beforeStep(dt); viewer::prepareVehicleScene(cars,scene);
        });
        secondMoved |= cars.velocity(1).z < -.5f;
        thirdMoved |= cars.velocity(2).z < -.5f;
        require(!cars.simulating(3),"A pileup activated an unrelated parked car");
    }
    require(secondMoved && thirdMoved,"Vehicle collisions did not transfer momentum through a parked-car pileup");
    require(cars.simulatedCars().empty(),"Cars activated by a pileup did not return to static physics after settling");
}
void settlingTarget() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(world);
    cars.add({0,.8f,0}); cars.add({0,.8f,-4.5f});
    cars.setDriven(0,true); cars.setDriven(1,true); cars.setDriven(1,false);
    auto filter = b3DefaultQueryFilter(); filter.maskBits = viewer::vehicleBodyCategory;
    b3World_OverlapAABB(world->id(),{{-2,-1,-7},{2,3,-2}},filter,[](b3ShapeId shape,void*) {
        b3Body_SetAwake(b3Shape_GetBody(shape),false); return true;
    },nullptr);
    // Sleeping is sufficient support for parking. Put the target just short
    // of the half-second transition before introducing the incoming vehicle.
    for (int step=0;step<29;++step) cars.beforeStep(viewer::PhysicsWorld::fixedStepSeconds);
    require(cars.simulating(1),"Settling target parked before the half-second grace period");
    cars.setPose(0,cars.pose(0),{0,0,-90});
    cars.beforeStep(viewer::PhysicsWorld::fixedStepSeconds);
    require(cars.simulating(1),"An incoming vehicle's target turned static immediately before collision");
}
void startingAgainstBumper() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(world);
    cars.add({0,.8f,0}); cars.add({0,.8f,-3.65f});
    cars.drive(0,1,0,false);
    cars.beforeStep(viewer::PhysicsWorld::fixedStepSeconds);
    require(cars.simulating(1),"Applying throttle against a parked bumper did not activate the target from rest");
}
}
int main() {
    try { parkedFleet(); transitions(); grenadeCategoryContact(); replicationAndAir(); sweptActivation(); pileupAndSettling(); settlingTarget(); startingAgainstBumper(); }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
