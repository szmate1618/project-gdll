#include "client/vehicle_playback.hpp"
#include "client/snapshots.hpp"
#include "vehicle_player.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
using namespace multiplayer;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void near(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .001f, message);
}

template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}

VehicleState car(std::uint32_t id, float x) {
    VehicleState state;
    state.id = id;
    state.chassis.center = {x, 1, 0};
    for (std::size_t i = 0; i < state.wheels.size(); ++i)
        state.wheels[i].center = {x + float(i), .5f, float(i) - 2};
    return state;
}

void coherentInterpolationAndStop() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    VehiclePlayback playback;
    const auto first = car(0, 0);
    auto last = car(0, 12);
    last.chassis.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
    last.velocity = {12, 0, 0};
    for (auto& wheel : last.wheels)
        wheel.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
    playback.push(60, {first});
    playback.push(72, {last});
    playback.apply(vehicles, 66);
    const auto pose = vehicles.pose(0);
    near(pose.chassis.center.x, 6, "Chassis did not interpolate between its own samples");
    near(vehicles.velocity(0).x, 6, "Velocity used a different playback time");
    const float diagonal = std::sqrt(.5f);
    const auto forward = pose.chassis.rotation * glm::vec3(0, 0, -1);
    near(forward.x, -diagonal, "Chassis rotation did not follow the short quaternion arc");
    near(forward.z, -diagonal, "Chassis rotation lost its interpolation angle");
    for (std::size_t i = 0; i < pose.wheels.size(); ++i) {
        near(pose.wheels[i][3].x, 6 + float(i), "Suspension position did not follow the chassis clock");
        const auto up = glm::mat3(pose.wheels[i]) * glm::vec3(0, 1, 0);
        near(up.y, diagonal, "Wheel rotation did not interpolate independently of the chassis");
        near(up.z, diagonal, "Wheel spin lost its quaternion interpolation");
    }
    last.velocity = {};
    playback.push(75, {last});
    playback.push(78, {});
    playback.apply(vehicles, 100);
    near(vehicles.pose(0).chassis.center.x, 12, "Omitted stopped car lost its final position");
    require(vehicles.velocity(0) == glm::vec3(0), "Stopped car retained its former velocity");
    const auto revision = vehicles.poseRevision(0);
    playback.push(81, {});
    playback.apply(vehicles, 103);
    require(vehicles.count() == 1 && !vehicles.simulating(0) && vehicles.poseRevision(0) == revision,
        "An omitted parked car was removed, simulated or moved again");
}

void independentSparseHistoriesAndReordering() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    VehiclePlayback playback;
    std::vector<VehicleState> initial;
    for (std::uint32_t id = 0; id < 6; ++id) initial.push_back(car(id, float(id) * 100));
    playback.push(60, initial);
    playback.push(72, {car(5, 512)});
    playback.apply(vehicles, 66);
    require(vehicles.count() == 6, "Sparse existing IDs changed the cached population");
    near(vehicles.pose(5).chassis.center.x, 506, "High sparse ID lost its independent interpolation history");
    near(vehicles.pose(0).chassis.center.x, 0, "An unrelated car update moved a parked car");
    playback.push(75, {car(0, 15)});
    playback.apply(vehicles, 73.5);
    near(vehicles.pose(0).chassis.center.x, 7.5f, "A second car used the first car's update interval");
    near(vehicles.pose(5).chassis.center.x, 512, "Omitting one moving car discarded its last pose");
    playback.push(74, {car(0, 999)});
    playback.apply(vehicles, 75);
    near(vehicles.pose(0).chassis.center.x, 15, "An out-of-order snapshot rewound a cached car");
}

void parkedIntervalBeforeMovement() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    VehiclePlayback playback;
    playback.push(3, {car(0, 20)});
    playback.apply(vehicles, 3);
    playback.push(300, {});
    playback.push(597, {});
    playback.apply(vehicles, 594);
    playback.push(600, {car(0, 20.6f)});
    playback.apply(vehicles, 594);
    near(vehicles.pose(0).chassis.center.x, 20, "New motion was smeared backward across a long parked interval");
    playback.apply(vehicles, 598.5);
    near(vehicles.pose(0).chassis.center.x, 20.3f, "First movement did not start after the last unchanged snapshot");
    playback.apply(vehicles, 600);
    near(vehicles.pose(0).chassis.center.x, 20.6f, "A previously parked car did not reach its new pose");
}

void batchValidationIsAtomic() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    VehiclePlayback playback;
    rejects([&] { playback.push(6, {car(5, 500)}); }, "A new population with missing IDs was accepted");
    playback.push(6, {car(0, 0)});
    rejects([&] { playback.push(12, {car(0, 10), car(2, 200)}); },
        "A sparse new ID with a population hole was accepted");
    playback.push(12, {car(1, 100)});
    playback.apply(vehicles, 12);
    require(vehicles.count() == 2, "Rejected batch left a partial population or consumed its tick");
    near(vehicles.pose(0).chassis.center.x, 0, "Rejected later ID published an earlier pose from the same batch");
    auto changedType = car(1, 101); changedType.type = 1;
    rejects([&] { playback.push(15, {car(0, 10), changedType}); }, "Vehicle type changed during a session");
    rejects([&] { playback.push(15, {car(0, 10), car(0, 11)}); }, "Duplicate car IDs were accepted");
    auto invalid = car(1, 101);
    invalid.wheels[3].center.y = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { playback.push(15, {car(0, 10), invalid}); }, "A nonfinite wheel pose was accepted");
    playback.push(15, {car(1, 101)});
    playback.apply(vehicles, 15);
    near(vehicles.pose(0).chassis.center.x, 0, "Invalid batch partially updated an otherwise valid car");
    near(vehicles.pose(1).chassis.center.x, 101, "Rejected batch prevented a corrected snapshot at the same tick");

    viewer::VehicleLayer empty(world);
    VehiclePlayback missingAsset;
    auto unavailable = car(1, 100); unavailable.type = 1;
    missingAsset.push(1, {car(0, 0), unavailable});
    rejects([&] { missingAsset.apply(empty, 1); }, "A car type absent from session assets was accepted");
    require(empty.count() == 0, "Asset validation created part of an invalid car population");
}

void sparseSnapshotsAndOccupancy(const Options& options) {
    Scene scene(options);
    Snapshots playback;
    Snapshot first; first.tick = 60; first.vehicles = {car(0, 0)};
    Snapshot moved; moved.tick = 72; moved.vehicles = {car(0, 12)};
    playback.push(first, 0);
    playback.push(moved, .2);
    playback.apply(scene, .2);
    near(scene.vehicles->pose(0).chassis.center.x, 6, "Shared world buffer no longer interpolates car motion");
    Snapshot omitted; omitted.tick = 75;
    playback.push(omitted, .25);
    playback.apply(scene, .25);
    near(scene.vehicles->pose(0).chassis.center.x, 9, "An omitted snapshot shortened an in-progress car interpolation");
    for (std::uint64_t tick = 78; tick <= 210; tick += 3) {
        omitted.tick = tick;
        const double now = double(tick - 60) / 60;
        playback.push(omitted, now);
        playback.apply(scene, now);
    }
    near(scene.vehicles->pose(0).chassis.center.x, 12, "Global snapshot retirement discarded a persistent car");
    Snapshot added; added.tick = 213; added.vehicles = {car(1, 100)};
    auto& driver = added.players.emplace_back(); driver.id = 1; driver.vehicle = 1; driver.seat = 0;
    playback.push(added, 2.55);
    playback.apply(scene, 2.55);
    require(scene.vehicles->count() == 2, "Newest occupied car was unavailable behind the render buffer");
    viewer::VehiclePlayer occupied; viewer::Camera camera;
    occupied.followCar(1, *scene.vehicles, camera);
    require(occupied.occupied() && glm::distance(camera.position(), viewer::vehicleSeat(*scene.vehicles, 1, 0)) < .001f,
        "Newest occupancy could not follow an immediately published car");
    Snapshot seated; seated.tick = 216; seated.players = added.players;
    const auto bytes = encode(seated);
    seated = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    playback.push(seated, 2.6);
    playback.apply(scene, 2.6);
    require(scene.vehicles->count() == 2, "Occupancy without repeated car records lost a cached car");
    seated.tick = 219; seated.players[0].vehicle = 2;
    playback.push(seated, 2.65);
    rejects([&] { playback.apply(scene, 2.65); }, "Occupancy referred to a car absent from persistent state");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        coherentInterpolationAndStop();
        independentSparseHistoriesAndReordering();
        parkedIntervalBeforeMovement();
        batchValidationIsAtomic();
        Options options; options.map = argv[1]; options.trees = options.forests = false; options.zombieCount = 0;
        sparseSnapshotsAndOccupancy(options);
        std::cout << "Sparse vehicle histories, parked intervals, rigid interpolation and cached occupancy passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
