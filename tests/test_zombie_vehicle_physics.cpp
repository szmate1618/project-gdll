#include "vehicle_player.hpp"
#include "zombie_layer.hpp"
#include "zombie_chase.hpp"
#include "zombie_navigation.hpp"
#include "simulation_clock.hpp"
#include "zombie_door_collision.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "interior_fixture.hpp"
#include <box3d/box3d.h>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Model ground(bool corridor, float curb) {
    viewer::Model model; model.materials.push_back({});
    interior_fixture::quad(model, {-80, 0, -80}, {-80, 0, 80}, {80, 0, 80}, {80, 0, -80}, "terrain");
    if (corridor) {
        interior_fixture::quad(model, {-2, 0, -4}, {2, 0, -4}, {2, 4, -4}, {-2, 4, -4}, "building_walls", glm::mat4(1), true);
        for (float x : {-1.15f, 1.15f})
            interior_fixture::quad(model, {x, 0, -5}, {x, 0, 5}, {x, 4, 5}, {x, 4, -5}, "building_walls", glm::mat4(1), true);
    }
    if (curb > 0) {
        interior_fixture::quad(model, {-2, curb, -2}, {2, curb, -2}, {2, curb, -8}, {-2, curb, -8}, "curb");
        interior_fixture::quad(model, {-2, 0, -2}, {2, 0, -2}, {2, curb, -2}, {-2, curb, -2}, "curb");
    }
    interior_fixture::bounds(model); model.boundsMax.y = 8;
    return model;
}
struct Fixture {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors;
    viewer::BoxLayer boxes{physics};
    viewer::VehicleLayer cars{physics};
    viewer::CollisionWorld world;
    viewer::PhysicsScene scene;
    viewer::ZombieLayer zombies;
    viewer::ZombieDoorCollision doorContacts{physics};
    Fixture(bool corridor = false, std::vector<viewer::DoorPanel> panels = {}, float curb = 0) : model(ground(corridor, curb)), doors(std::move(panels), physics),
        world(model, {}, &doors), scene(physics, world, model.boundsMin, model.boundsMax) {
        world.setVehicles(&cars); world.setBoxes(&boxes);
        cars.add({0, .8f, 0}); cars.setDriven(0, true); zombies.assets.emplace_back(); zombies.attachPhysics(physics);
    }
    void add(glm::vec3 p) {
        viewer::ZombieInstance z; z.restRoot = z.restTransform = z.transform = glm::translate(glm::mat4(1), p);
        zombies.instances.push_back(z);
    }
    void tick(int frames = 1, float dt = 1.f / 60) {
        for (int frame = 0; frame < frames; ++frame) physics->step(dt, [&](float seconds) {
            scene.prepare(boxes); viewer::prepareVehicleScene(cars, scene);
            zombies.prepareVehiclePhysics(cars, scene, world, seconds, &doors);
            doorContacts.prepare(doors, zombies); cars.beforeStep(seconds);
        }, [&](float seconds) { zombies.finishVehiclePhysics(cars, seconds, 0); zombies.synchronizePhysics(); });
    }
    void moveCar(glm::vec3 delta) {
        auto pose = cars.pose(0); pose.chassis.center += delta;
        for (auto& wheel : pose.wheels) wheel[3] += glm::vec4(delta, 0);
        cars.setPose(0, pose, {});
    }
};
void noEmbedding(const Fixture& f) {
    const auto chassis = f.cars.pose(0).chassis;
    for (const auto& z : f.zombies.instances) {
        if (z.ragdoll || z.removed) continue;
        for (const auto& contact : viewer::boxContacts(chassis, glm::vec3(z.restRoot[3]), .3f, 1.8f, 0))
            require(contact.depth < .035f, "Physical living capsules must not remain embedded in the chassis");
        require(std::abs(z.restRoot[3].y) < .08f, "A packed flat-ground crowd must stay grounded");
    }
}
void blockedCrowd() {
    Fixture f(true);
    for (int row = 0; row < 3; ++row) for (int col = -1; col <= 1; ++col) f.add({col * .62f, 0, -2.12f - row * .62f});
    f.zombies.rebuildSpatialIndex(); f.tick(60);
    f.cars.drive(0, 1, 0, false);
    for (int frame = 0; frame < 240; ++frame) {
        f.tick(); noEmbedding(f);
        for (const auto& z : f.zombies.instances)
            require(z.restRoot[3].z > -3.73f, "The rear row must remain outside the wall under car pressure");
    }
    require(f.zombies.ragdollCount() == 0 && f.cars.pose(0).chassis.center.z > -1.1f &&
            glm::length(f.cars.velocity(0)) < .3f, "A pinned crowd must constrain the car rather than clip through it");
}
void pushingAndSelection() {
    Fixture f;
    for (int row = 0; row < 27; ++row) f.add({0, 0, -2.12f - row * .62f});
    f.add({40, 0, 0}); f.zombies.rebuildSpatialIndex(); f.tick(30);
    require(f.zombies.vehiclePhysicsCount() == 27 && f.zombies.instances[26].vehiclePhysics && !f.zombies.instances[27].vehiclePhysics,
            "Touching-group activation must include distant back rows and exclude unrelated zombies");
    const auto before = f.zombies.instances[26].restRoot[3].z;
    f.cars.drive(0, .35f, 0, false);
    for (int frame = 0; frame < 120; ++frame) { f.tick(); noEmbedding(f); }
    require(f.zombies.instances[26].restRoot[3].z < before - .05f, "Contact pressure must propagate through the entire group");
    f.cars.drive(0, 0, 0, true); f.moveCar({40, 0, 0}); f.tick(180);
    require(!f.zombies.instances[0].vehiclePhysics && !f.zombies.instances[26].vehiclePhysics,
            "A settled group far from cars must return to the ordinary crowd controller");
    const auto id = std::size_t(27);
    require(f.zombies.instances[id].vehiclePhysics, "The newly nearby zombie must activate without scanning visibility");
    f.zombies.instances[id].removed = true; f.tick();
    require(!f.zombies.instances[id].vehiclePhysics, "Removed zombies must release physical capsules");
}
void walkingAndObjects() {
    Fixture f; f.moveCar({5, 0, 0}); f.add({0, 0, 0}); f.zombies.rebuildSpatialIndex();
    // Allow idle capsules to sleep, then verify that a new AI request wakes them.
    f.boxes.add({0, .5f, -2}, {.5f, 1, .5f}); f.tick(180);
    const auto boxBefore = f.boxes.pose(0).center;
    for (int frame = 0; frame < 240; ++frame) {
        auto& z = f.zombies.instances[0]; const auto before = z.restRoot;
        viewer::moveZombieToward(z, f.zombies.assets[0], {0, 0, -8}, 1.2f, 1.f / 60, f.world, 0);
        require(z.restRoot == before, "AI must request physical movement without directly moving the capsule");
        f.tick();
    }
    require(f.boxes.pose(0).center.z < boxBefore.z - .2f && f.zombies.instances[0].restRoot[3].z < -1,
            "A physical walking zombie must be able to push a light movable object");
    require(f.zombies.instances[0].walkSeconds > .5, "Physical walking must advance locomotion animation from actual travel");
}
void doorsAndHandoff() {
    viewer::DoorPanel panel; panel.hinge = {.6f, 1, -2}; panel.center = {0, 1, -2};
    panel.halfExtents = {.6f, 1, .04f}; panel.outward = {0, 0, -1};
    Fixture f(false, {panel}); f.moveCar({5, 0, 0}); f.add({0, 0, -1.55f});
    f.zombies.rebuildSpatialIndex(); f.tick(30);
    require(f.doors.activeCount() == 1 && f.doorContacts.count() == 0,
            "Physical zombies must activate doors and replace duplicate door-only blockers");
    for (int frame = 0; frame < 120; ++frame) {
        viewer::moveZombieToward(f.zombies.instances[0], f.zombies.assets[0], {0, 0, -5}, 1.2f, 1.f / 60, f.world, 0);
        f.tick();
    }
    require(std::abs(f.doors.angle(0)) > .2f, "Physical contact must be able to swing an unlatched door");
    require(f.zombies.knockDown(0, {0, 1, 0}, 0) && f.zombies.vehiclePhysicsCount() == 0,
            "Ragdoll handoff must immediately remove the upright capsule");
    f.tick(10);
    require(f.zombies.vehiclePhysicsCount() == 0 && f.zombies.instances[0].ragdoll,
            "A ragdoll must never reacquire a living capsule");
}
void neighborQuery() {
    viewer::ZombieCrowd crowd; crowd.reset(3); crowd.update(0, {1e20f, 0, 1e20f});
    crowd.update(1, {0, 0, 0}); crowd.update(2, {.6f, 3, 0});
    std::vector<std::size_t> ids;
    crowd.queryNear({1e20f, 0, 1e20f}, .7f, ids);
    require(ids.size() == 1 && ids[0] == 0, "Neighbor queries must terminate at large finite coordinates");
    crowd.queryNear({}, .7f, ids);
    require(ids.size() == 1 && ids[0] == 1, "Connected crowds must not bridge separate floors");
}
void fastApproach() {
    Fixture f; f.tick(60); f.add({0, 0, -4.4f}); f.zombies.rebuildSpatialIndex();
    f.cars.drive(0, 0, 0, false);
    f.cars.applyImpulse(0, {0, 0, -1100 * 30}, f.cars.pose(0).chassis.center);
    const float initialSpeed = -f.cars.velocity(0).z;
    for (int frame = 0; frame < 12; ++frame) { f.tick(); noEmbedding(f); }
    require(f.zombies.ragdollCount() == 1 && -f.cars.velocity(0).z < initialSpeed - .5f,
            "A fast approach must create a ragdoll and transfer contact momentum without tunneling");
}
void curbsAndFrameRates() {
    Fixture curb(false, {}, .2f); curb.moveCar({5, 0, 0}); curb.add({0, 0, -.8f});
    curb.zombies.rebuildSpatialIndex(); curb.tick(30);
    for (int frame = 0; frame < 240; ++frame) {
        viewer::moveZombieToward(curb.zombies.instances[0], curb.zombies.assets[0], {0, .2f, -6}, 1.2f, 1.f / 60, curb.world, 0);
        curb.tick();
    }
    const auto feet = glm::vec3(curb.zombies.instances[0].restRoot[3]);
    std::cout << "curb feet=" << feet.x << ',' << feet.y << ',' << feet.z << '\n';
    require(feet.z < -3 && feet.y > .18f && feet.y < .24f,
            "Physical locomotion must retain bounded step-up assistance for a low curb");
    Fixture slow, fast;
    for (auto* f : {&slow, &fast}) {
        f->moveCar({5, 0, 0}); f->add({0, 0, 0}); f->zombies.rebuildSpatialIndex(); f->tick(30);
        viewer::moveZombieToward(f->zombies.instances[0], f->zombies.assets[0], {0, 0, -8}, 1.2f, 1.f / 60, f->world, 0);
    }
    slow.tick(60, 1.f / 30); fast.tick(240, 1.f / 120);
    require(glm::distance(glm::vec3(slow.zombies.instances[0].restRoot[3]), glm::vec3(fast.zombies.instances[0].restRoot[3])) < .01f,
            "Physical locomotion must run once per shared fixed step, independently of rendering");
}
void grenadeRagdollHandoff() {
    Fixture f;f.moveCar({5,0,0});f.add({0,0,0});f.zombies.rebuildSpatialIndex();f.tick(30);
    require(f.zombies.instances[0].vehiclePhysics,"Blast fixture did not acquire a physical capsule");
    require(f.zombies.explode({0,.9f,1},3,0,f.world)==1 && f.zombies.ragdollCount()==1 &&
            f.zombies.vehiclePhysicsCount()==0 && !f.zombies.instances[0].vehiclePhysics,
            "Grenade blast did not hand a vehicle-adjacent capsule to the current ragdoll system");
    f.tick(10);
    require(f.zombies.vehiclePhysicsCount()==0,"An explosion victim reacquired an upright capsule");
}
void scenarioCapsules() {
    Fixture f;f.moveCar({5,0,0});f.add({0,0,0});f.zombies.rebuildSpatialIndex();
    using namespace viewer::navmesh_path;
    auto mesh=Mesh({{-70,-70},{70,-70},{70,70},{-70,70}},{{0,4}},
                   {{0,noPolygon},{1,noPolygon},{2,noPolygon},{3,noPolygon}});
    f.zombies.enableNavigation(std::make_unique<viewer::ZombieNavigation>(std::move(mesh),1));
    f.zombies.setScenarioDestinations({{0,0,-4}});
    f.zombies.setPerceptionEnabled(false);
    f.tick(30);
    require(f.zombies.instances[0].vehiclePhysics,"Scenario fixture did not activate a nearby capsule");
    for(int i=0;i<180;++i) {
        const auto before=f.zombies.instances[0].restRoot;
        viewer::SimulationClock::instance().advance(1.f/60);
        f.zombies.updateBehavior(1.f/60,{{10000,0,10000}},f.world,0);
        require(f.zombies.instances[0].restRoot==before,"Scenario navigation teleported a physical capsule");
        f.tick();
    }
    require(f.zombies.instances[0].restRoot[3].z<-2 && f.zombies.instances[0].walkSeconds>.5,
            "Scenario route did not advance through physical walking and animation");
}
void benchmark() {
    for (int count : {0, 50, 200}) {
        Fixture f;
        for (int id = 0; id < count; ++id) f.add({(id % 10 - 4.5f) * .62f, 0, -2.12f - (id / 10) * .62f});
        f.zombies.rebuildSpatialIndex(); f.tick(60); f.cars.drive(0, .35f, 0, false);
        std::vector<double> samples; double total = 0;
        for (int frame = 0; frame < 120; ++frame) {
            const auto start = std::chrono::steady_clock::now(); f.tick();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            samples.push_back(ms); total += ms;
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "Synthetic pushing: population=" << count << " active=" << f.zombies.vehiclePhysicsCount()
                  << " mean_ms=" << total / samples.size() << " p95_ms=" << samples[samples.size() * 95 / 100] << '\n';
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--benchmark") { benchmark(); return 0; }
        const auto start = std::chrono::steady_clock::now();
        grenadeRagdollHandoff(); scenarioCapsules(); neighborQuery(); blockedCrowd(); pushingAndSelection(); walkingAndObjects(); doorsAndHandoff(); curbsAndFrameRates(); fastApproach();
        std::cout << "Living capsule regressions passed in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
