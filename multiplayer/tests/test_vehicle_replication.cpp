#include "server/vehicle_replication.hpp"
#include "vehicle_layer.hpp"
#include "physics_world.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<VehicleState> snapshot(VehicleReplication& replication,
        const viewer::VehicleLayer& vehicles, std::uint64_t tick) {
    std::vector<VehicleState> result;
    replication.append(vehicles, tick, result);
    return result;
}
void move(viewer::VehicleLayer& vehicles, std::size_t id, glm::vec3 delta, glm::vec3 velocity = {}) {
    auto pose = vehicles.pose(id);
    pose.chassis.center += delta;
    for (auto& wheel : pose.wheels) wheel[3] += glm::vec4(delta, 0);
    vehicles.setPose(id, pose, velocity);
}

void parkedFleetAndLateJoin() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world);
    for (std::size_t id = 0; id < 1000; ++id) vehicles.add({float(id) * 6, .8f, 0});
    VehicleReplication first, lateJoin;
    const auto initial = snapshot(first, vehicles, 3);
    require(initial.size() == 1000 && initial.back().id == 999, "Initial fleet was incomplete");
    // Delay the first receipt while another snapshot is generated. Retrying
    // must not postpone the first frame's ability to confirm this same state.
    require(snapshot(first, vehicles, 6).size() == 1000, "Unconfirmed initial fleet was not retried");
    first.acknowledge(3);
    require(snapshot(first, vehicles, 9).empty(), "Resends postponed confirmation of unchanged cars");
    require(snapshot(lateJoin, vehicles, 9).size() == 1000, "Late join inherited another peer's receipts");
    vehicles.add({6000, .8f, 0});
    const auto added = snapshot(first, vehicles, 12);
    require(added.size() == 1 && added[0].id == 1000, "New car resent the acknowledged fleet");
}

void tickZeroAndUnsentReceipts() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world); vehicles.add({0, .8f, 0});
    VehicleReplication replication;
    replication.acknowledge(0); replication.acknowledge(1000);
    require(snapshot(replication, vehicles, 0).size() == 1, "Pre-send receipt suppressed the baseline");
    replication.acknowledge(0);
    require(snapshot(replication, vehicles, 3).size() == 1, "Default ACK zero confirmed a lost baseline");
    replication.acknowledge(0); replication.acknowledge(1000);
    require(snapshot(replication, vehicles, 6).size() == 1, "Future or zero receipt confirmed unsent state");
    replication.acknowledge(3);
    require(snapshot(replication, vehicles, 9).empty(), "Positive baseline receipt was ignored");
}

void reorderedReceiptsAndFinalPose() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world); vehicles.add({0, .8f, 0}); vehicles.add({6, .8f, 0});
    VehicleReplication replication;
    snapshot(replication, vehicles, 3); replication.acknowledge(3);
    move(vehicles, 1, {0, 0, -2}, {0, 0, -3});
    auto moving = snapshot(replication, vehicles, 6);
    require(moving.size() == 1 && moving[0].id == 1 && moving[0].chassis.center.z == -2,
        "Moving car did not produce an isolated update");
    move(vehicles, 1, {0, 0, -3});
    const auto stopped = snapshot(replication, vehicles, 9);
    require(stopped.size() == 1 && stopped[0].chassis.center.z == -5 && stopped[0].velocity == glm::vec3(0),
        "Final parked state was not published");
    replication.acknowledge(6); replication.acknowledge(3);
    const auto retry = snapshot(replication, vehicles, 12);
    require(retry.size() == 1 && retry[0].chassis.center.z == -5,
        "Delayed receipt confirmed a newer undelivered final state");
    replication.acknowledge(12);
    require(snapshot(replication, vehicles, 15).empty(), "Received final pose never became quiet");
}

void replacementAtSameTick() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world); vehicles.add({0, .8f, 0});
    VehicleReplication replication;
    snapshot(replication, vehicles, 3); replication.acknowledge(3);
    move(vehicles, 0, {2, 0, 0});
    require(snapshot(replication, vehicles, 3).size() == 1, "Same-tick replacement was omitted");
    replication.acknowledge(3);
    require(snapshot(replication, vehicles, 6).size() == 1, "Old same-tick receipt confirmed a replacement");
    replication.acknowledge(3);
    require(snapshot(replication, vehicles, 9).size() == 1, "Repeated old receipt confirmed the replacement");
    replication.acknowledge(6);
    require(snapshot(replication, vehicles, 12).empty(), "Later replacement receipt was not accepted");
}

void velocityAndWheelChanges() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer vehicles(world); vehicles.add({0, .8f, 0});
    VehicleReplication replication;
    snapshot(replication, vehicles, 3); replication.acknowledge(3);
    const auto revision = vehicles.poseRevision(0);
    auto pose = vehicles.pose(0);
    vehicles.setPose(0, pose, {2, 0, 0});
    require(vehicles.poseRevision(0) == revision, "Velocity fixture unexpectedly changed pose revision");
    const auto velocity = snapshot(replication, vehicles, 6);
    require(velocity.size() == 1 && velocity[0].velocity.x == 2, "Velocity-only change was omitted");
    replication.acknowledge(6);
    vehicles.setPose(0, pose, {});
    require(vehicles.poseRevision(0) == revision, "Stop fixture unexpectedly changed pose revision");
    require(snapshot(replication, vehicles, 9).size() == 1, "Velocity-only final stop was omitted");
    replication.acknowledge(9);
    const auto spin = glm::angleAxis(.5f, glm::vec3(1, 0, 0));
    pose.wheels[0] = glm::translate(glm::mat4(1), glm::vec3(pose.wheels[0][3])) * glm::mat4_cast(spin);
    vehicles.setPose(0, pose, {});
    const auto wheel = snapshot(replication, vehicles, 12);
    require(wheel.size() == 1 && std::abs(glm::dot(wheel[0].wheels[0].rotation, spin)) > .9999f,
        "Wheel-only pose change was omitted");
    replication.acknowledge(12);
    vehicles.setPose(0, pose, {});
    require(snapshot(replication, vehicles, 15).empty(), "Identical explicit poses resent a parked car");
}

void layerReplacement() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer original(world), replacement(world);
    original.add({0, .8f, 0}); replacement.add({20, .8f, 0});
    VehicleReplication replication;
    snapshot(replication, original, 3); replication.acknowledge(3);
    require(snapshot(replication, original, 6).empty(), "Acknowledged original layer was not quiet");
    const auto changed = snapshot(replication, replacement, 9);
    require(changed.size() == 1 && changed[0].chassis.center.x == 20,
        "Replacement layer reused a previous layer's cached state");
    replication.acknowledge(6);
    require(snapshot(replication, replacement, 12).size() == 1, "Old layer receipt confirmed replacement data");
    replication.acknowledge(9);
    require(snapshot(replication, replacement, 15).empty(), "Replacement layer receipt was ignored");
}
} // namespace

int main() {
    try {
        parkedFleetAndLateJoin(); tickZeroAndUnsentReceipts(); reorderedReceiptsAndFinalPose();
        replacementAtSameTick(); velocityAndWheelChanges(); layerReplacement();
        std::cout << "Persistent vehicle updates and snapshot receipts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
