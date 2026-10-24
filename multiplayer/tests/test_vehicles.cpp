#include "server/world.hpp"
#include "client/prediction.hpp"
#include "client/snapshots.hpp"
#include "vehicle_player.hpp"
#include "simulation_clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <json.hpp>
#include "file_fingerprint.hpp"
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
Options options(const char* asset) {
    Options result; result.map = asset; result.trees = result.forests = false; result.zombieCount = 0;
    return result;
}
void openGround(Scene& scene) {
    viewer::Model model; viewer::Primitive p;
    for (const glm::vec3 point : {glm::vec3(-120, 0, -120), {-120, 0, 120}, {120, 0, 120}, {120, 0, -120}}) {
        viewer::Vertex vertex; vertex.position = point; p.vertices.push_back(vertex);
    }
    p.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(p);
    viewer::DrawInstance draw; draw.name = "terrain"; model.draws.push_back(draw);
    model.boundsMin = {-120, 0, -120}; model.boundsMax = {120, 8, 120};
    scene.model = std::move(model);
    scene.doors = std::make_unique<viewer::DoorLayer>(std::vector<viewer::DoorPanel>{}, scene.physics);
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model);
    scene.collision->setBoxes(scene.boxes.get()); scene.collision->setVehicles(scene.vehicles.get());
    scene.spawnEye = {-2.8f, 1.7f, -.25f};
}
Snapshot wire(const Snapshot& snapshot) {
    const auto bytes = encode(snapshot);
    return std::get<Snapshot>(decode(bytes.data(), bytes.size()));
}
void protocolChecks() {
    Snapshot s; s.vehicles.push_back({});
    s.vehicles[0].chassis.center = {2, .8f, -5};
    s.vehicles[0].wheels[0].rotation = glm::angleAxis(.7f, glm::vec3(1, 0, 0));
    PlayerState p; p.id = 1; p.vehicle = 0; p.seat = 0; s.players.push_back(p);
    const auto copy = wire(s);
    require(copy.players[0].vehicle == 0 && copy.vehicles[0].chassis.center == s.vehicles[0].chassis.center &&
            copy.vehicles[0].wheels[0].rotation == s.vehicles[0].wheels[0].rotation, "Vehicle wire pose lost rigid state");
    s.players.push_back(p); s.players.back().id = 2;
    rejects([&] { encode(s); }, "Two drivers occupied one car on the wire");
    s.players.back().seat = 1;
    require(wire(s).players[1].seat == 1, "Distinct passengers in one car must survive the wire");
    s.players.back().seat = 4;
    rejects([&] { encode(s); }, "A fifth seat was accepted");
    s.players.pop_back(); s.players[0].vehicle = 1;
    rejects([&] { encode(s); }, "Driver referred to an absent car");
    s.players[0].vehicle = 0; s.vehicles[0].wheels[2].rotation = {0, 0, 0, 0};
    rejects([&] { encode(s); }, "Invalid wheel quaternion accepted");
    Command c; c.sequence = 1; c.enterVehicle = true;
    const auto bytes = encode(Commands{{c}});
    require(std::get<Commands>(decode(bytes.data(), bytes.size())).commands[0].enterVehicle, "Enter press lost on wire");
}
void parkedPopulation(const Options& o, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto map=directory/"map.glb", sidecar=directory/"abandoned_cars.instances.json";
    std::filesystem::copy_file(o.map,map,std::filesystem::copy_options::overwrite_existing);
    nlohmann::json document={{"schema","godollo.abandoned-car-placements"},{"version",1},
        {"map",{{"map_sha256",viewer::fileSha256(map)}}},
        {"transforms",{{"units","meters"},{"asset_axes",{{"forward","+Z"},{"up","+Y"},{"right","+X"}}}}},
        {"counts",{{"instances",1000}}},{"instances",nlohmann::json::array()}};
    for(int i=0;i<1000;++i) document["instances"].push_back({{"id",std::to_string(i)},
        {"position",{float(i%40)*6-100,.015f,float(i/40)*6-100}},{"rotation_xyzw",{0,0,0,1}}});
    {std::ofstream(sidecar)<<document;}
    auto selected=o; selected.map=map;
    Scene server(selected),client(selected);
    require(server.vehiclePlacements && server.vehiclePlacements->size()==1000 && server.vehicles->count()==0,
            "Scene did not load placement data without assigning client-side car types");
    require(client.fingerprint==server.fingerprint,"Identical placement publications changed session identity");
    World world(server);
    require(server.vehicles->count()==1000,"Server did not instantiate every parked car");
    Scene repeated(selected);
    World repeatedWorld(repeated);
    for (std::size_t i=0;i<1000;++i)
        require(repeated.vehicles->type(i)==server.vehicles->type(i),"Reloading the level changed parked car types");
    require(server.vehicles->simulatedCars().empty(),"Server activated suspension for the parked population");
    const auto observer=world.join();
    const auto state=wire(world.snapshot(observer));
    require(state.vehicles.size()==1000,"Full parked population was not replicated");
    Snapshots playback;playback.push(state,0);playback.apply(client,0);
    require(client.vehicles->count()==1000,"Late join lost parked cars");
    require(client.vehicles->simulatedCars().empty(),"Client allocated physics for replicated cars");
    std::set<std::size_t> types;
    for(std::size_t i=0;i<1000;++i) {
        types.insert(server.vehicles->type(i));
        require(client.vehicles->type(i)==server.vehicles->type(i),"Clients rerandomized vehicle types");
        require(glm::distance(client.vehicles->pose(i).chassis.center,server.vehicles->pose(i).chassis.center)<1e-5f,
                "Client lost a parked-car world pose");
    }
    require(types.size()>1,"Server assigned a fixed type to the parked population");
    document["instances"][0]["position"][0]=99;
    {std::ofstream(sidecar)<<document;}
    Scene changed(selected);
    require(changed.fingerprint!=server.fingerprint,"Parked car positions were not fingerprinted");
    // Exercise all independent population caps together. Increasing vehicle
    // capacity must still fit the existing bounded fragment/message budget.
    Snapshot full;
    full.vehicles.resize(maxVehicles);full.zombies.resize(maxVisibleZombies);full.boxes.resize(maxBoxes);
    full.doors.resize(maxDoors);full.players.resize(maxPlayers);
    for(std::size_t i=0;i<full.vehicles.size();++i) full.vehicles[i].id=static_cast<std::uint32_t>(i);
    for(std::size_t i=0;i<full.zombies.size();++i) full.zombies[i].id=static_cast<std::uint32_t>(i);
    for(std::size_t i=0;i<full.boxes.size();++i) full.boxes[i].id=static_cast<std::uint32_t>(i);
    for(std::size_t i=0;i<full.doors.size();++i) full.doors[i].id=static_cast<std::uint32_t>(i);
    for(std::size_t i=0;i<full.players.size();++i) full.players[i].id=static_cast<std::uint32_t>(i+1);
    require(encode(full).size()<=maxMessageBytes && wire(full).vehicles.size()==maxVehicles,
            "Maximum legal population no longer fits a complete snapshot");
}
void ownershipAndDriving(const Options& o) {
    Scene scene(o); openGround(scene); scene.vehicles->add({0, .8f, 0});
    World world(scene);
    const auto first = world.join();
    scene.spawnEye.x -= 1; // Both competitors stand at the same driver-only entry region.
    const auto second = world.join();
    scene.spawnEye.x += 1;
    for (int i = 0; i < 120; ++i) world.step();
    Command enter; enter.sequence = 1; enter.enterVehicle = true;
    // Duplicate/redundant packets and a simultaneous competing driver.
    world.submit(first, Commands{{enter}}); world.submit(first, Commands{{enter}});
    world.submit(second, Commands{{enter}});
    for (int i = 0; i < 6; ++i) world.step();
    auto state = wire(world.snapshot(first));
    require(state.players[0].vehicle == 0 && state.players[1].vehicle == noVehicle,
            "Driver admission must be exclusive and Enter must execute once");
    require(glm::distance(world.players().at(first).camera.position(), viewer::vehicleSeat(*scene.vehicles,0,0)) < .001f,
            "Driver camera must follow the physical seat");
    std::uint64_t sequence = 1;
    Command drive; drive.movement.forward = 1; drive.left = true; drive.reload = true; drive.slot = 2; drive.interaction = true;
    for (int i = 0; i < 120; ++i) {
        drive.sequence = ++sequence;
        world.submit(first, Commands{{drive}}); world.step();
    }
    require(scene.vehicles->pose(0).chassis.center.z < -4 && -scene.vehicles->velocity(0).z > 4,
            "Server driving must use physical forward thrust");
    require(world.players().at(first).shots == 0 && world.players().at(first).weapons.slot() == 0 &&
            !world.players().at(first).interaction.heldBox(viewer::Hand::Left), "Driving must suspend weapons and grabbing");
    auto moving = wire(world.snapshot(first));
    Scene client(o); Snapshots playback; playback.push(moving, 0); playback.apply(client, 0);
    require(client.vehicles->count() == 1 && glm::distance(client.vehicles->pose(0).chassis.center,
            scene.vehicles->pose(0).chassis.center) < .001f, "Late join lost the moving car");
    for (std::size_t i = 0; i < 4; ++i)
        require(glm::distance(glm::vec3(client.vehicles->pose(0).wheels[i][3]),
                glm::vec3(scene.vehicles->pose(0).wheels[i][3])) < .001f, "Late join lost wheel suspension poses");
    Prediction prediction; auto initialDriver = moving.players[0]; initialDriver.resolved = 0;
    prediction.reconcile(initialDriver, *client.collision);
    const auto seated = prediction.eye();
    prediction.advance(drive, *client.collision);
    require(prediction.eye() == seated, "Driving input must not predict walking through the car");
    drive.enterVehicle = true; drive.sequence = ++sequence;
    world.submit(first, Commands{{drive}}); world.step();
    require(world.snapshot(first).players[0].vehicle == 0, "Enter must not eject a driver from a moving car");
    // Packet starvation neutralizes thrust and applies brakes without freeing
    // the seat. A real disconnect frees it and leaves the parked car replicated.
    for (int i = 0; i < 240; ++i) world.step();
    require(glm::length(scene.vehicles->velocity(0)) < .4f && world.snapshot(first).players[0].vehicle == 0,
            "Timed-out controls must stop the car and retain seat ownership");
    enter.sequence = ++sequence; world.submit(first, Commands{{enter}}); world.step();
    require(world.snapshot(first).players[0].vehicle == noVehicle, "Enter must exit a stopped car beside supported ground");
    enter.sequence = ++sequence; world.submit(first, Commands{{enter}}); world.step();
    require(world.snapshot(first).players[0].vehicle == 0, "Stopped driver must be able to re-enter");
    world.leave(first);
    require(world.snapshot(second).players[0].vehicle == noVehicle && world.snapshot(second).vehicles.size() == 1,
            "Disconnect must release the seat without deleting the car");
    // A fresh player can claim the released car; observers cannot steal it by
    // sending throttle without first obtaining the driver's seat.
    const auto box = scene.vehicles->pose(0).chassis;
    scene.spawnEye = box.center + box.rotation * glm::vec3(-2.6f, .9f, -.25f);
    scene.spawnEye.x -= static_cast<float>((second + 1 - 1) % maxPlayers);
    const auto third = world.join();
    enter.sequence = 1; world.submit(third, Commands{{enter}});
    for (int i = 0; i < 6; ++i) world.step();
    require(world.snapshot(third).players.back().vehicle == 0, "Disconnected driver left a stale seat reservation");
}
void passengerOnly(const Options& o) {
    Scene scene(o); openGround(scene); scene.vehicles->add({0,.68f,0});
    const auto box=scene.vehicles->pose(0).chassis;
    scene.spawnEye=box.center+scene.vehicles->config(0).seats[1].entryPosition+glm::vec3(.6f,0,0);
    World world(scene); const auto player=world.join();
    Command enter;enter.sequence=1;enter.enterVehicle=true;
    world.submit(player,Commands{{enter}});
    for(int i=0;i<6;++i) world.step();
    require(world.snapshot(player).players[0].seat==1 && !scene.vehicles->simulating(0),
            "Passenger entry activated driving physics");
    world.leave(player);
    require(!scene.vehicles->simulating(0),"Passenger disconnect activated driving physics");
}
void passengers(const Options& o) {
    Scene scene(o); openGround(scene); scene.vehicles->add({0,.8f,0},0,1);
    World world(scene);
    for (int i=0;i<120;++i) world.step();
    std::vector<std::uint32_t> occupants;
    for (std::size_t seat=0;seat<4;++seat) {
        const auto box=scene.vehicles->pose(0).chassis;
        auto point=scene.vehicles->config(0).seats[seat].entryPosition;
        point.x+=point.x<0 ? -.6f : .6f;
        scene.spawnEye=box.center+box.rotation*point;
        scene.spawnEye.x-=static_cast<float>(seat);
        const auto id=world.join(); occupants.push_back(id);
        Command enter; enter.sequence=1; enter.enterVehicle=true;
        world.submit(id,Commands{{enter}}); world.submit(id,Commands{{enter}});
        for (int i=0;i<6;++i) world.step();
        const auto state=wire(world.snapshot(id));
        require(state.players.back().vehicle==0 && state.players.back().seat==seat,
                "Each door must admit one player to its configured seat, with duplicate Enter deduplicated");
    }
    const auto box=scene.vehicles->pose(0).chassis;
    scene.spawnEye=box.center+box.rotation*glm::vec3(-1.7f,.9f,-.25f)-glm::vec3(4,0,0);
    const auto extra=world.join();
    Command enter; enter.sequence=1; enter.enterVehicle=true;
    world.submit(extra,Commands{{enter}});
    for (int i=0;i<6;++i) world.step();
    auto snapshot=wire(world.snapshot(extra));
    require(snapshot.players.back().resolved==1 && snapshot.players.back().vehicle==noVehicle,"A fifth player entered an occupied four-seat car");
    Scene client(o); Snapshots playback; playback.push(snapshot,0); playback.apply(client,0);
    require(client.vehicles->type(0)==1,"Late join lost the configured car type");
    viewer::VehiclePlayer passenger; viewer::Camera camera;
    passenger.followCar(0,*client.vehicles,camera,1);
    require(passenger.occupied() && !passenger.driving() && !passenger.toggleThirdPerson(*client.vehicles,camera,*client.collision),
            "Passenger obtained driver controls or chase camera");
    camera.look(90,30); const auto direction=camera.direction();
    passenger.updateCamera(*client.vehicles,camera,*client.collision);
    require(glm::distance(camera.position(),viewer::vehicleSeat(*client.vehicles,0,1))<.001f &&
            glm::distance(direction,camera.direction())<.001f,"Passenger mouse look moved the seat or was reset");
    Command driver; driver.sequence=1; driver.movement.forward=1;
    Command rider; rider.sequence=1; rider.movement.forward=-1; rider.movement.right=1; rider.movement.jump=true;
    rider.left=true; rider.reload=true; rider.slot=2; rider.interaction=true; rider.pitch=25; rider.movement.yawDegrees=40;
    for (int i=0;i<120;++i) {
        ++driver.sequence; ++rider.sequence;
        world.submit(occupants[0],Commands{{driver}}); world.submit(occupants[1],Commands{{rider}}); world.step();
    }
    require(scene.vehicles->velocity(0).z < -3,"Passenger throttle/brake/steering overrode the driver");
    const auto& seated=world.players().at(occupants[1]);
    require(seated.shots==0 && seated.weapons.slot()==0 && !seated.interaction.heldBox(viewer::Hand::Left) &&
            std::abs(seated.camera.pitchDegrees()-25)<.001f,"Passenger used weapons/grabbing or lost mouse look");
    world.leave(occupants[2]);
    require(world.snapshot(occupants[0]).players[0].seat==0,"Passenger disconnect removed the driver");
    for (int i=0;i<240;++i) { ++driver.sequence; driver.movement.forward=0; driver.movement.jump=true;
        world.submit(occupants[0],Commands{{driver}}); world.step(); }
    enter.sequence=++rider.sequence; world.submit(occupants[1],Commands{{enter}}); world.step();
    snapshot=wire(world.snapshot(occupants[0]));
    require(snapshot.players[1].vehicle==noVehicle && snapshot.players[0].vehicle==0,
            "Passenger Enter did not release only its own seat beside a stopped car");
}

void interpolation(const Options& o) {
    Scene scene(o);
    Snapshot a; a.tick = 10; a.vehicles.push_back({});
    a.vehicles[0].chassis.center = {0, 1, 0};
    for (auto& wheel : a.vehicles[0].wheels) wheel.center = {0, .5f, 0};
    Snapshot b = a; b.tick = 22; b.vehicles[0].chassis.center.x = 12;
    b.vehicles[0].chassis.rotation = glm::angleAxis(1.f, glm::vec3(0, 1, 0));
    for (auto& wheel : b.vehicles[0].wheels) { wheel.center.x = 12; wheel.rotation = b.vehicles[0].chassis.rotation; }
    Snapshots playback; playback.push(wire(a), 0); playback.push(wire(b), .2); playback.apply(scene, .2);
    const auto pose = scene.vehicles->pose(0);
    require(std::abs(pose.chassis.center.x - 6) < .001f && std::abs(pose.wheels[0][3].x - 6) < .001f &&
            std::abs(glm::length(pose.chassis.rotation) - 1) < .001f, "Car and wheels must interpolate as coherent rigid poses");
    require(!playback.push(a, .3), "Old car state replaced a newer snapshot");
    viewer::VehiclePlayer driver; viewer::Camera camera;
    driver.followCar(0, *scene.vehicles, camera);
    require(driver.toggleThirdPerson(*scene.vehicles, camera, *scene.collision) && driver.thirdPerson(),
            "Server-confirmed driver must allow third person");
    driver.followCar(0, *scene.vehicles, camera);
    require(driver.thirdPerson(), "Repeated occupancy snapshots reset camera mode");
    driver.followCar({}, *scene.vehicles, camera);
    require(!driver.thirdPerson() && !driver.toggleThirdPerson(*scene.vehicles, camera, *scene.collision),
            "Leaving the car must reset and disable third person");
}
void zombieImpacts(const Options& o) {
    for (float speed : {1.f, 9.f}) {
        Scene scene(o); openGround(scene); scene.vehicles->add({0, .8f, 0});
        World world(scene); const auto observer = world.join();
        for (int i = 0; i < 120; ++i) world.step();
        scene.zombies.assets.emplace_back();
        auto& asset = scene.zombies.assets[0];
        asset.skeleton.resize(1); asset.boneFrames = {glm::mat4(1), glm::mat4(1)};
        asset.frameCount = 2; asset.duration = 1;
        const auto chassis = scene.vehicles->pose(0).chassis;
        // Start at this fitted model's front bumper, rather than inside a
        // longer car at the old 3.6 m prototype's hardcoded contact position.
        const glm::vec3 contactFeet(chassis.center.x,0,chassis.center.z-chassis.halfExtents.z-viewer::ZombieCrowd::bodyRadius+.02f);
        viewer::ZombieInstance zombie;
        zombie.restRoot = zombie.restTransform = zombie.transform = glm::translate(glm::mat4(1), contactFeet);
        scene.zombies.instances.push_back(zombie); scene.zombies.rebuildSpatialIndex();
        scene.vehicles->drive(0, 0, 0, false);
        scene.vehicles->applyImpulse(0, {0, 0, -1100 * speed}, chassis.center);
        world.step();
        const auto snapshot = wire(world.snapshot(observer));
        require(snapshot.zombies.size() == 1, "Observer lost the impacted zombie");
        if (speed < 5) {
            require(!snapshot.zombies[0].dead && scene.zombies.instances[0].restRoot[3].z < contactFeet.z-.01f,
                    "Slow server car impacts must push living zombies");
        } else {
            require(snapshot.zombies[0].dead && world.nextPose(observer).has_value(),
                    "Fast server car impacts must replicate ragdolls and their poses");
        }
    }
}
void physicsParity(const Options& o) {
    Scene server(o), local(o); openGround(server); openGround(local);
    server.vehicles->add({0, .8f, 0}); local.vehicles->add({0, .8f, 0});
    World world(server); const auto id = world.join();
    viewer::PhysicsScene localScene(local.physics, *local.collision, local.model.boundsMin, local.model.boundsMax);
    const auto localStep = [&] {
        local.physics->step(tickSeconds, [&](float seconds) {
            viewer::prepareVehicleScene(*local.vehicles, localScene); local.vehicles->beforeStep(seconds);
        });
    };
    for (int i = 0; i < 120; ++i) { world.step(); localStep(); }
    Command command; command.sequence = 1; command.enterVehicle = true;
    local.vehicles->setDriven(0,true);
    world.submit(id, Commands{{command}}); world.step(); localStep();
    require(world.snapshot(id).players[0].vehicle == 0, "Parity fixture must occupy the car");
    command.enterVehicle = false; command.movement.forward = 1; command.movement.right = .3f;
    local.vehicles->drive(0, 1, .3f, false);
    for (int i = 0; i < 120; ++i) {
        ++command.sequence; world.submit(id, Commands{{command}}); world.step(); localStep();
    }
    require(glm::distance(server.vehicles->pose(0).chassis.center, local.vehicles->pose(0).chassis.center) < .01f &&
            glm::distance(server.vehicles->velocity(0), local.vehicles->velocity(0)) < .01f,
            "Multiplayer must preserve single-player acceleration, directional grip and steering physics");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected test map and scratch directory");
        protocolChecks(); const auto o = options(argv[1]); ownershipAndDriving(o); passengerOnly(o); passengers(o); interpolation(o); zombieImpacts(o); physicsParity(o); parkedPopulation(o,argv[2]);
        std::cout << "Multiplayer vehicle ownership, physics, late join, timeout, camera and wire checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
