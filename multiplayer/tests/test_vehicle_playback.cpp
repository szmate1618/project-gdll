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

void rotationNear(glm::quat actual, glm::quat expected, const char* message) {
    near(glm::length(actual), 1, "Predicted chassis orientation lost normalization");
    require(std::abs(glm::dot(actual, expected)) > .99999f, message);
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

void linearDriverExtrapolation() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    VehiclePlayback playback;
    auto moving = car(0, .3f);
    moving.velocity = {6, 1, -2};
    moving.chassis.rotation = glm::angleAxis(.4f, glm::vec3(0, 1, 0));
    auto first = car(0, 0); first.chassis.rotation = moving.chassis.rotation;
    playback.push(60, {first, car(1, 10)});
    playback.push(63, {moving, car(1, 12)});
    playback.apply(vehicles, 61.5, VehiclePlayback::Driver{0, 64.5});
    const auto pose = vehicles.pose(0);
    near(pose.chassis.center.x, .45f, "Driver used buffered position instead of current linear prediction");
    near(pose.chassis.center.y, 1.025f, "Driver prediction lost vertical linear velocity");
    near(pose.chassis.center.z, -.05f, "Driver prediction lost lateral linear velocity");
    require(std::abs(glm::dot(pose.chassis.rotation, moving.chassis.rotation)) > .99999f,
        "Linear prediction changed the authoritative chassis orientation");
    for (std::size_t i = 0; i < pose.wheels.size(); ++i)
        require(glm::distance(glm::vec3(pose.wheels[i][3]) - pose.chassis.center,
                    moving.wheels[i].center - moving.chassis.center) < .001f,
            "Predicted wheels drifted away from their chassis");
    near(vehicles.pose(1).chassis.center.x, 11, "Driver prediction removed the remote car's playback buffer");
    playback.apply(vehicles, 61.5, VehiclePlayback::Driver{0, 65.1});
    near(vehicles.pose(0).chassis.center.x, .51f, "Driver position did not advance between server updates");
    playback.apply(vehicles, 61.5);
    near(vehicles.pose(0).chassis.center.x, .15f, "Leaving prediction failed to restore an unchanged buffered tick");
    playback.apply(vehicles, 61.5, VehiclePlayback::Driver{0, 1000});
    near(vehicles.pose(0).chassis.center.x, 1.8f, "Packet loss allowed unbounded driving extrapolation");
    playback.apply(vehicles, 61.5, VehiclePlayback::Driver{0, 2000});
    near(vehicles.pose(0).chassis.center.x, 1.8f, "Driver continued drifting beyond its prediction horizon");
    playback.push(66, {car(0, .6f)}); // An authoritative stop replaces the extrapolation immediately.
    playback.apply(vehicles, 64.5, VehiclePlayback::Driver{0, 67.5});
    near(vehicles.pose(0).chassis.center.x, .6f, "Authoritative stop retained predicted travel");
    playback.push(69, {});
    playback.push(68, {moving}); // A reordered motion update cannot restart the stopped car.
    playback.apply(vehicles, 67.5, VehiclePlayback::Driver{0, 72});
    near(vehicles.pose(0).chassis.center.x, .6f, "Sparse or stale updates restarted stopped prediction");
    require(vehicles.simulatedCars().empty(), "Driver extrapolation activated client vehicle physics");
    rejects([&] { playback.apply(vehicles, 67.5, VehiclePlayback::Driver{0, std::numeric_limits<double>::quiet_NaN()}); },
        "Nonfinite driver prediction time was accepted");
}

void angularDriverExtrapolation() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    VehiclePlayback playback;
    auto first = car(0, 0);
    first.chassis.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
    playback.push(60, {first});
    playback.apply(vehicles, 60, VehiclePlayback::Driver{0, 62});
    rotationNear(vehicles.pose(0).chassis.rotation, first.chassis.rotation,
        "First orientation sample guessed angular motion without a previous sample");
    // A world-space turn around a tilted axis exercises yaw, pitch and roll,
    // and does not commute with the initial chassis rotation.
    const auto axis = glm::normalize(glm::vec3(1, 2, 3));
    const auto atAngle = [&](float degrees) {
        return glm::angleAxis(glm::radians(degrees), axis) * first.chassis.rotation;
    };
    auto next = car(0, .3f);
    next.velocity = {6, 0, 0};
    next.chassis.rotation = -atAngle(3); // Quaternion sign must not change the inferred turn.
    next.wheels[0].rotation = glm::angleAxis(.7f, glm::vec3(1, 0, 0));
    playback.push(63, {next});
    playback.apply(vehicles, 61.5, VehiclePlayback::Driver{0, 64.5});
    const auto pose = vehicles.pose(0);
    rotationNear(pose.chassis.rotation, atAngle(4.5f), "Driver did not extrapolate full chassis orientation");
    near(pose.chassis.center.x, .45f, "Angular prediction changed the chassis translation");
    const auto extraTurn = glm::angleAxis(glm::radians(1.5f), axis);
    for (std::size_t wheel = 0; wheel < pose.wheels.size(); ++wheel) {
        require(glm::distance(glm::vec3(pose.wheels[wheel][3]), pose.chassis.center +
                    extraTurn * (next.wheels[wheel].center - next.chassis.center)) < .001f,
            "Wheel center did not rotate around the predicted chassis pivot");
        rotationNear(glm::quat_cast(glm::mat3(pose.wheels[wheel])), extraTurn * next.wheels[wheel].rotation,
            "Predicted chassis turn lost the wheel's relative orientation");
    }
    playback.apply(vehicles, 63, VehiclePlayback::Driver{0, 65}); // Retires the older pose sample.
    rotationNear(vehicles.pose(0).chassis.rotation, atAngle(5), "History retirement lost measured angular velocity");
    next.chassis.rotation = atAngle(6); next.chassis.center.x = .6f;
    playback.push(66, {next});
    playback.apply(vehicles, 63, VehiclePlayback::Driver{0, 67.5});
    rotationNear(vehicles.pose(0).chassis.rotation, atAngle(7.5f), "New samples reset a steady angular prediction");
    playback.push(65, {first});
    playback.apply(vehicles, 63, VehiclePlayback::Driver{0, 67.5});
    rotationNear(vehicles.pose(0).chassis.rotation, atAngle(7.5f), "Reordered snapshot changed angular velocity");
    for (const double tick : {1000.0, 2000.0}) {
        playback.apply(vehicles, 66, VehiclePlayback::Driver{0, tick});
        rotationNear(vehicles.pose(0).chassis.rotation, atAngle(21), "Packet loss allowed unbounded angular extrapolation");
    }
    playback.push(69, {});
    playback.apply(vehicles, 66, VehiclePlayback::Driver{0, 72});
    rotationNear(vehicles.pose(0).chassis.rotation, atAngle(6), "Omitted unchanged car continued predicted rotation");
    next.chassis.center.x = .9f;
    playback.push(72, {next});
    playback.apply(vehicles, 69, VehiclePlayback::Driver{0, 75});
    rotationNear(vehicles.pose(0).chassis.rotation, atAngle(6), "Confirmed rotation stop retained old angular velocity");
}

void angularPredictionBoundaries() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    for (const auto interval : {3u, 60u}) {
        VehiclePlayback playback;
        auto first = car(0, 0), next = first;
        first.chassis.rotation = glm::angleAxis(glm::radians(179.f), glm::vec3(0, 1, 0));
        next.chassis.rotation = glm::angleAxis(glm::radians(-179.f), glm::vec3(0, 1, 0));
        playback.push(60, {first}); playback.push(60 + interval, {next});
        playback.apply(vehicles, 60, VehiclePlayback::Driver{0, 61.5 + interval});
        const float expected = interval == 3 ? -178.f : -179.f;
        rotationNear(vehicles.pose(0).chassis.rotation,
            glm::angleAxis(glm::radians(expected), glm::vec3(0, 1, 0)),
            "Heading wrap or a long sample gap produced incorrect angular prediction");
    }
    VehiclePlayback jump;
    auto next = car(0, 0); next.chassis.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
    jump.push(60, {car(0, 0)}); jump.push(63, {next});
    jump.apply(vehicles, 60, VehiclePlayback::Driver{0, 66});
    rotationNear(vehicles.pose(0).chassis.rotation, next.chassis.rotation,
        "An orientation discontinuity produced a rapid predicted spin");
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
void localDriverSnapshots(const Options& options) {
    Scene scene(options);
    Snapshots playback;
    Snapshot first; first.tick = 60;
    first.vehicles = {car(0, 0), car(1, 10)};
    PlayerState driver; driver.id = 1; driver.vehicle = 0; driver.seat = 0;
    PlayerState remote; remote.id = 2; remote.vehicle = 1; remote.seat = 0;
    first.players = {driver, remote};
    playback.push(first, 0, .05);
    auto moved = first; moved.tick = 63;
    moved.vehicles = {car(0, .3f), car(1, 10.3f)};
    moved.vehicles[0].chassis.rotation = glm::angleAxis(.05f, glm::vec3(0, 1, 0));
    for (auto& vehicle : moved.vehicles) vehicle.velocity = {6, 0, 0};
    playback.push(moved, .05, .05);
    playback.apply(scene, .05, 1);
    near(scene.vehicles->pose(0).chassis.center.x, .6f, "Local driver prediction omitted incoming transit");
    near(scene.vehicles->pose(1).chassis.center.x, 10, "A remote driver bypassed the world buffer");
    viewer::VehiclePlayer occupied; viewer::Camera camera;
    occupied.followCar(0, *scene.vehicles, camera);
    camera.setOrientation(25, 7);
    playback.apply(scene, .075, 1);
    occupied.updateCamera(*scene.vehicles, camera, *scene.collision);
    near(scene.vehicles->pose(0).chassis.center.x, .75f, "Driver prediction did not run between snapshots");
    require(glm::distance(camera.position(), viewer::vehicleSeat(*scene.vehicles, 0, 0)) < .001f,
        "Driver camera did not follow the predicted chassis");
    near(camera.yawDegrees(), 25 - glm::degrees(.025f), "Predicted turn lost the driver's relative mouse yaw");
    near(camera.pitchDegrees(), 7, "Orientation prediction overwrote local mouse pitch");

    auto passenger = moved; passenger.tick = 66; passenger.players[0].seat = 1;
    passenger.vehicles = {car(0, .6f)};
    playback.push(passenger, .1, .05);
    playback.apply(scene, .1, 1);
    near(scene.vehicles->pose(0).chassis.center.x, 0, "Passenger retained local driver extrapolation");
    playback.apply(scene, .1, 2);
    near(scene.vehicles->pose(1).chassis.center.x, 10.9f, "Prediction selected the wrong local player's car");
    auto exited = passenger; exited.tick = 69;
    exited.players[0].vehicle = noVehicle; exited.players[0].seat = noVehicleSeat;
    exited.vehicles.clear();
    playback.push(exited, .15, .05);
    playback.apply(scene, .15, 1);
    near(scene.vehicles->pose(0).chassis.center.x, .3f, "Exiting a car failed to restore buffered playback");
    require(scene.vehicles->simulatedCars().empty(), "Snapshot prediction allocated detailed vehicle physics");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        coherentInterpolationAndStop();
        linearDriverExtrapolation();
        angularDriverExtrapolation();
        angularPredictionBoundaries();
        independentSparseHistoriesAndReordering();
        parkedIntervalBeforeMovement();
        batchValidationIsAtomic();
        Options options; options.map = argv[1]; options.trees = options.forests = false; options.zombieCount = 0;
        sparseSnapshotsAndOccupancy(options);
        localDriverSnapshots(options);
        std::cout << "Sparse vehicle histories, rigid interpolation, local driver extrapolation and occupancy passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
