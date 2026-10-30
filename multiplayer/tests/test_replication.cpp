#include "outgoing_snapshots.hpp"
#include "fragments.hpp"
#include "server/replication.hpp"
#include "client/snapshots.hpp"
#include "zombie_replication_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void pacedSnapshots() {
    OutgoingSnapshots outgoing;
    // A valid large snapshot at the protocol's maximum prop/door capacities.
    Snapshot snapshot; snapshot.tick = 1;
    snapshot.zombies.resize(maxZombieUpdatesPerSnapshot);
    for (std::size_t i = 0; i < snapshot.zombies.size(); ++i) snapshot.zombies[i].id = static_cast<std::uint32_t>(i);
    snapshot.doors.resize(maxDoors);
    for (std::size_t i = 0; i < snapshot.doors.size(); ++i) snapshot.doors[i].id = static_cast<std::uint32_t>(i);
    snapshot.boxes.resize(maxBoxes);
    for (std::size_t i = 0; i < snapshot.boxes.size(); ++i) snapshot.boxes[i].id = static_cast<std::uint32_t>(i);
    outgoing.enqueue(encode(snapshot));
    Fragments fragments;
    std::vector<std::uint64_t> delivered;
    std::size_t attempts = 0;
    const auto send = [&](const auto& bytes) {
        // Model recurring transport backpressure without any packet loss.
        if (++attempts % 7 == 0) return false;
        const auto part = std::get<Fragment>(decode(bytes.data(), bytes.size()));
        if (const auto complete = fragments.add(part))
            delivered.push_back(std::get<Snapshot>(decode(complete->data(), complete->size())).tick);
        return true;
    };
    for (int tick = 0; tick < 200; ++tick) {
        snapshot.tick = static_cast<std::uint64_t>(tick + 2);
        outgoing.enqueue(encode(snapshot));
        outgoing.pump(2000, send);
    }
    for (int tick = 0; tick < 1000 && !outgoing.empty(); ++tick) outgoing.pump(2000, send);
    require(outgoing.empty() && delivered.size() >= 3, "Backpressure produced only incomplete snapshot prefixes");
    require(delivered.front() == 1 && delivered.back() == 201, "Active/newest snapshot was lost");
    require(delivered.size() < 20, "Obsolete pending snapshots accumulated instead of coalescing");
}
void poseProtocol() {
    CorpsePose pose; pose.id = 7; pose.version = 123; pose.bones.resize(maxBones, glm::mat4(1));
    pose.severedJoint = 9; pose.tick = 120; pose.sleeping = true;
    const auto bytes = encode(pose);
    const auto copy = std::get<CorpsePose>(decode(bytes.data(), bytes.size()));
    require(copy.id == 7 && copy.version == 123 && copy.tick == 120 && copy.sleeping && copy.bones == pose.bones && copy.severedJoint == 9,
            "Independent pose wire encoding lost separation or skinning state");
    auto invalidJoint = bytes; invalidJoint.back() = 255;
    bool invalidRejected = false;
    try { decode(invalidJoint.data(), invalidJoint.size()); } catch (const std::runtime_error&) { invalidRejected = true; }
    require(invalidRejected, "Invalid severed joint was accepted from the wire");
    const auto ack = encode(PoseAck{7, 123});
    require(std::get<PoseAck>(decode(ack.data(), ack.size())).version == 123, "Pose acknowledgement lost its version");
    for (std::size_t size = 0; size < bytes.size(); size += 31) {
        bool rejected = false;
        try { decode(bytes.data(), size); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Truncated bulk pose accepted");
    }
    pose.bones.emplace_back(1);
    bool rejected = false;
    try { encode(pose); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Bulk pose bypassed the skeleton budget");
}
void fiveHzPoseCadence(Scene& scene) {
    scene.zombies = viewer::ZombieLayer{};
    scene.zombies.assets.emplace_back().skeleton.resize(1);
    scene.zombies.instances.emplace_back();
    scene.zombies.setRagdollPalette(0, {glm::mat4(1)});
    Replication replication;
    Snapshot state; state.tick = 120;
    test_replication::readySnapshot(replication, scene, {0, 0, 0}, state);
    const auto first = replication.nextPose(scene);
    require(first && first->tick == 120, "Initial pose lacks its authoritative timestamp");
    replication.sent(*first); replication.acknowledge({first->id, first->version});
    ++scene.zombies.instances[0].ragdollPoseVersion;
    state.tick = 131; replication.snapshot(scene, {0, 0, 0}, state);
    require(!replication.nextPose(scene), "Ragdoll exceeded five-Hz update cadence");
    state.tick = 132; replication.snapshot(scene, {0, 0, 0}, state);
    const auto next = replication.nextPose(scene);
    require(next && next->tick == 132 && next->version > first->version,
        "Five-Hz update did not send the newest complete pose");
    scene.zombies = viewer::ZombieLayer{};
}
void corpseTransfers(Scene& scene) {
    constexpr std::size_t corpseCount = 140;
    scene.zombies.assets.emplace_back();
    scene.zombies.assets[0].skeleton.resize(maxBones);
    for (std::size_t i = 0; i < corpseCount; ++i) {
        viewer::ZombieInstance instance;
        instance.transform[3] = {static_cast<float>(i), 0, 0, 1};
        scene.zombies.instances.push_back(instance);
        scene.zombies.setRagdollPalette(i, std::vector<glm::mat4>(maxBones, glm::mat4(1)));
    }
    Replication replication;
    Snapshot base; base.tick = 1; base.players.push_back({}); base.players[0].id = 1;
    const auto essential = test_replication::readySnapshot(replication, scene, {0, 0, 0}, base);
    require(essential.zombies.size() == corpseCount && encode(essential).size() < 7000,
        "Bulk corpse palettes inflated essential state");
    std::vector<CorpsePose> active;
    for (std::size_t i = 0; i < Replication::maxPoseTransfers; ++i) {
        const auto pose = replication.nextPose(scene);
        require(pose && pose->id == i, "Bulk selection is not fair/deterministic");
        replication.sent(*pose); active.push_back(*pose);
    }
    require(active.size() == 100, "Peer must allow 100 distinct corpse transfers before acknowledgement");
    require(!replication.nextPose(scene), "Slow peer exceeded the in-flight pose bound");
    // Keep changing the first corpse while all transfers are blocked.
    for (int i = 0; i < 100; ++i) {
        ++scene.zombies.instances[0].ragdollPoseVersion;
        base.tick++;
        require(replication.snapshot(scene, {0, 0, 0}, base).tick == base.tick,
            "Bulk backpressure blocked essential snapshots");
        require(!replication.nextPose(scene), "Changing poses accumulated unbounded versions");
    }
    for (const auto& pose : active) replication.acknowledge({pose.id, pose.version});
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i < corpseCount; ++i) {
        const auto pose = replication.nextPose(scene);
        require(pose.has_value(), "Unsent or changed corpse pose was lost");
        if (pose->id == 0) require(pose->version == scene.zombies.instances[0].ragdollPoseVersion,
            "Bulk transfer queued obsolete intermediate versions");
        replication.sent(*pose); replication.acknowledge({pose->id, pose->version}); seen.insert(pose->id);
        if (!replication.nextPose(scene)) break;
    }
    require(seen.size() == corpseCount - active.size() + 1 && seen.count(0),
        "Moving corpses starved other corpse transfers");
    Replication lateJoin;
    test_replication::readySnapshot(lateJoin, scene, {0, 0, 0}, base);
    require(lateJoin.nextPose(scene).has_value(), "Late join lost initial corpse poses");
    // Unknown/future acknowledgements must not release transfer slots.
    bool rejected = false;
    try { lateJoin.acknowledge({0, 1000}); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Unsent pose acknowledgement accepted");
}
void retiredBoxes(Scene& scene, const Options& options) {
    scene.boxes->add({0,1,0},{1,1,1});
    scene.boxes->add({2,1,0},{.5f,1,.5f});
    Replication replication;
    Snapshot base; base.tick = 60;
    const auto initial = replication.snapshot(scene,{},base);
    Scene client(options); Snapshots playback;
    playback.push(initial,0); playback.apply(client,0);
    const auto oldBody = client.boxes->prop(0);
    scene.boxes->setPose(0,{0,-32,0},{1,0,0,0});
    scene.boxes->retireBelow(-30);
    base.tick += 60;
    const auto retired = replication.snapshot(scene,{},base);
    require(retired.boxes.size() == 2 && retired.boxes[0].removed && !retired.boxes[1].removed,
            "Server must preserve retired box IDs in snapshots");
    playback.push(retired,1); playback.apply(client,1);
    require(!oldBody.valid() && !client.boxes->active(0) && client.boxes->active(1) &&
            client.boxes->pose(1).halfExtents.x == .25f, "Client retirement must preserve surviving box identities and dimensions");
    Scene late(options); Snapshots latePlayback;
    latePlayback.push(retired,0); latePlayback.apply(late,0);
    require(late.boxes->count() == 2 && !late.boxes->active(0) && late.boxes->active(1),
            "Late joins must reconstruct removed slots without leaving a live body");
    require(scene.boxes->add({4,1,0},{1,1,1}) == 2, "Spawn after retirement must append a new ID");
    base.tick += 60;
    playback.push(replication.snapshot(scene,{},base),2); playback.apply(client,2);
    require(client.boxes->count() == 3 && client.boxes->active(2) && !client.boxes->active(0),
            "Later snapshots must not resurrect retired bodies or shift IDs");
}
void turning(Scene& scene) {
    scene.zombies = viewer::ZombieLayer{};
    scene.zombies.assets.emplace_back(); scene.zombies.instances.emplace_back();
    // Normalization deliberately contains a reflection, nonuniform scale and
    // shear: immediate playback must preserve the whole fixed affine transform.
    auto normalization = glm::scale(glm::mat4(1), glm::vec3(-2, 3, 4));
    normalization[1].x = .3f; normalization[3] = {1, 2, 3, 1};
    Snapshot a; a.tick = 60; a.zombies.emplace_back(); a.zombies[0].transform = normalization;
    auto b = a; b.tick = 90;
    b.zombies[0].root = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(0, 1, 0));
    b.zombies[0].root[3] = {10, 0, 0, 1};
    b.zombies[0].transform = b.zombies[0].root * normalization;
    Snapshots playback;
    ZombieSetup setup; setup.version = 1; setup.normalization = normalization;
    playback.pushSetups(scene, ZombieSetups{{setup}});
    playback.push(a, 0); playback.apply(scene, 0);
    playback.push(b, .5); playback.apply(scene, .5);
    const auto& instance = scene.zombies.instances[0];
    require(std::abs(glm::length(glm::vec3(instance.transform[0])) - 2) < .0001f &&
        std::abs(glm::length(glm::vec3(instance.transform[2])) - 4) < .0001f,
        "Turning changed zombie size");
    require(std::abs(glm::determinant(glm::mat3(instance.transform)) + 24) < .001f,
        "Turning lost asset reflection/shear");
    require(std::abs(instance.restRoot[3].x - 10) < .001f, "Latest root position retained a playback delay");
    const auto forward = glm::normalize(glm::vec3(instance.restRoot[2]));
    require(std::abs(forward.x - 1) < .001f,
        "Latest rigid turn retained a playback delay");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        pacedSnapshots(); poseProtocol();
        Options options; options.map = argv[1]; options.trees = options.forests = false; options.zombieCount = 0;
        Scene scene(options); retiredBoxes(scene, options); fiveHzPoseCadence(scene); corpseTransfers(scene); turning(scene);
        std::cout << "Pacing, independent corpse transfers and immediate rigid playback passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
