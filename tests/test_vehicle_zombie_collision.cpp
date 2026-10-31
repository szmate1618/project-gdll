#include "vehicle_zombie_collision.hpp"
#include "vehicle_player.hpp"
#include "zombie_layer.hpp"
#include "zombie_navigation.hpp"
#include "simulation_clock.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "interior_fixture.hpp"
#include <box3d/box3d.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Model ground(bool wall) {
    viewer::Model model; model.materials.emplace_back();
    interior_fixture::quad(model,{-80,0,-80},{-80,0,80},{80,0,80},{80,0,-80},"terrain");
    if (wall) interior_fixture::quad(model,{-10,0,-2},{10,0,-2},{10,4,-2},{-10,4,-2},"building_walls",glm::mat4(1),true);
    interior_fixture::bounds(model); model.boundsMax.y=8; return model;
}
struct Fixture {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars;
    viewer::CollisionWorld world;
    viewer::PhysicsScene scene;
    viewer::ZombieLayer zombies;
    viewer::VehicleZombieCollision impacts;
    Fixture(bool wall=false,bool active=true,viewer::VehicleConfig config={},float yaw=0)
        : model(ground(wall)),cars(physics,{config}),world(model),scene(physics,world,model.boundsMin,model.boundsMax) {
        world.setVehicles(&cars); cars.add({0,.8f,0},yaw); if(active) cars.setDriven(0,true);
        zombies.assets.emplace_back(); zombies.attachPhysics(physics);
    }
    void add(glm::vec3 feet) {
        viewer::ZombieInstance z; z.restRoot=z.restTransform=z.transform=glm::translate(glm::mat4(1),feet);
        zombies.instances.push_back(z);
    }
    void tick(int frames) {
        for(int n=0;n<frames;++n) physics->step(1.f/60,[&](float step) {
            viewer::prepareVehicleScene(cars,scene); cars.beforeStep(step); impacts.capture(cars);
        },[&](float step) { impacts.resolve(cars,zombies,world,step,0); });
    }
    void move(glm::vec3 delta,glm::vec3 velocity) {
        auto pose=cars.pose(0); pose.chassis.center+=delta;
        for(auto& wheel:pose.wheels) wheel[3]+=glm::vec4(delta,0);
        cars.setPose(0,pose,velocity);
    }
};
void thresholdAndPublication() {
    for(float speed:{4.9f,5.1f}) {
        Fixture f; f.add({0,0,-2.12f}); f.zombies.rebuildSpatialIndex();
        f.impacts.capture(f.cars); f.move({0,0,-.08f},{0,0,-speed});
        f.impacts.resolve(f.cars,f.zombies,f.world,1.f/60,0);
        std::vector<std::size_t> ids;
        if(speed<5) {
            require(!f.zombies.instances[0].ragdoll && f.zombies.instances[0].restRoot[3].z<-2.2f,
                    "A slow chassis impact must push the living zombie");
            require(!f.zombies.instances[0].vehicleImpactRagdoll, "A slow push must not mark a vehicle ragdoll");
            f.zombies.queryLivingNear(glm::vec3(f.zombies.instances[0].restRoot[3]),.01f,ids);
            require(ids==std::vector<std::size_t>{0},"Pushing left the spatial membership behind");
            require(f.cars.velocity(0).z>-speed,"A pushed zombie supplied no reaction impulse to the car");
        } else {
            require(f.zombies.ragdollCount()==1 && !f.zombies.instances[0].springRagdoll,
                    "A fast chassis hit must create an ordinary loose ragdoll");
            require(f.zombies.instances[0].vehicleImpactRagdoll, "A car knockdown must retain its replication cause");
            f.zombies.queryLivingNear({0,0,-2.12f},3,ids);
            require(ids.empty(),"Car ragdoll remained in living collision queries");
            f.zombies.removeRagdoll(0);
            require(!f.zombies.instances[0].vehicleImpactRagdoll, "Retiring a car corpse retained its replication cause");
        }
    }
}
void normalDrivingKnocksDown() {
    Fixture f; f.tick(120); f.add({0,0,-12}); f.zombies.rebuildSpatialIndex();
    f.cars.drive(0,1,0,false);
    for(int n=0;n<240 && !f.zombies.instances[0].ragdoll;++n) f.tick(1);
    require(f.zombies.ragdollCount()==1,"A normally accelerating car failed to ragdoll its victim");
}
void sweptRotatedAndOccluded() {
    Fixture sweep; sweep.add({0,0,-3}); sweep.zombies.rebuildSpatialIndex();
    sweep.impacts.capture(sweep.cars); sweep.move({0,0,-7},{0,0,-20});
    sweep.impacts.resolve(sweep.cars,sweep.zombies,sweep.world,.1f,0);
    require(sweep.zombies.ragdollCount()==1,"Fast travel skipped a zombie between chassis poses");
    viewer::VehicleConfig config; config.dimensions={2.4f,1.9f,5.8f};
    Fixture rotated(false,true,config,glm::half_pi<float>()); rotated.add({-3.22f,0,0}); rotated.zombies.rebuildSpatialIndex();
    rotated.impacts.capture(rotated.cars); rotated.move({-.08f,0,0},{-6,0,0});
    rotated.impacts.resolve(rotated.cars,rotated.zombies,rotated.world,1.f/60,0);
    require(rotated.zombies.ragdollCount()==1,"Scaled, turned cars used the old prototype collision dimensions");
    Fixture occluded(true); occluded.add({0,0,-2.25f}); occluded.zombies.rebuildSpatialIndex();
    occluded.impacts.capture(occluded.cars); occluded.move({0,0,-.2f},{0,0,-9});
    occluded.impacts.resolve(occluded.cars,occluded.zombies,occluded.world,1.f/60,0);
    require(!occluded.zombies.instances[0].ragdoll && occluded.zombies.instances[0].restRoot[3].z==-2.25f,
            "A wall did not shield a zombie from a nearby chassis");
}
void livingCrowdStaysKinematic() {
    Fixture f; f.tick(120);
    for(int n=0;n<2000;++n) f.add({float(n%40)-20,0,-4-float(n/40)});
    f.zombies.rebuildSpatialIndex();
    const auto before=b3World_GetCounters(f.physics->id()); f.tick(10);
    const auto after=b3World_GetCounters(f.physics->id());
    require(before.bodyCount==after.bodyCount && before.shapeCount==after.shapeCount && before.jointCount==after.jointCount,
            "Driving near a living crowd allocated experimental native zombie bodies");
    Fixture parked(false,false);
    for(int n=1;n<1000;++n) parked.cars.add({float(n%40)*4,1,float(n/40)*5});
    parked.add({0,0,-2.12f}); parked.zombies.rebuildSpatialIndex(); parked.tick(2);
    require(parked.cars.simulatedCars().empty() && parked.zombies.ragdollCount()==0 &&
            parked.zombies.instances[0].restRoot[3].z==-2.12f && b3World_GetCounters(parked.physics->id()).jointCount==0,
            "Parked fleets acquired wheel/zombie simulation or pushed stationary bystanders");
}
void scenarioKeepsNormalMovement() {
    Fixture f; f.add({8,0,0}); f.zombies.rebuildSpatialIndex();
    using namespace viewer::navmesh_path;
    auto mesh=Mesh({{-70,-70},{70,-70},{70,70},{-70,70}},{{0,4}},
                   {{0,noPolygon},{1,noPolygon},{2,noPolygon},{3,noPolygon}});
    f.zombies.enableNavigation(std::make_unique<viewer::ZombieNavigation>(std::move(mesh),1));
    f.zombies.setScenarioDestinations({{8,0,-4}}); f.zombies.setPerceptionEnabled(false);
    const auto before=b3World_GetCounters(f.physics->id()).bodyCount;
    for(int n=0;n<120;++n) {
        viewer::SimulationClock::instance().advance(1.f/60);
        f.zombies.updateBehavior(1.f/60,{{10000,0,10000}},f.world,0);
    }
    require(f.zombies.instances[0].restRoot[3].z<-2 && f.zombies.instances[0].walkSeconds>.5 &&
            before==b3World_GetCounters(f.physics->id()).bodyCount,
            "Nearby cars replaced normal scenario navigation with native capsule motors");
}
}
int main() {
    try {
        thresholdAndPublication(); normalDrivingKnocksDown(); sweptRotatedAndOccluded();
        livingCrowdStaysKinematic(); scenarioKeepsNormalMovement();
        std::cout<<"Restored swept vehicle/zombie contacts passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
