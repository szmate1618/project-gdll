#include "protocol.hpp"
#include "fragments.hpp"
#include "client/prediction.hpp"
#include "client/snapshots.hpp"
#include "server/world.hpp"
#include "zombie_animation.hpp"
#include "simulation_clock.hpp"
#include "zombie_replication_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
void reportFire(multiplayer::Command& command) {
    if (command.left && command.slot < 4) command.firearmShot = multiplayer::FirearmShot{command.slot, {}};
    else command.firearmShot.reset();
}
multiplayer::FirearmHit headReport() {
    multiplayer::FirearmHit report;
    report.hit.zombie = 0; report.hit.headshot = true; report.hit.point = {0, 1.7f, 0};
    return report;
}
viewer::Model ground() {
    viewer::Model model;
    viewer::Primitive p;
    for (const glm::vec3 v : {glm::vec3(-100, 0, -100), {-100, 0, 100}, {100, 0, 100}, {100, 0, -100}}) {
        viewer::Vertex vertex; vertex.position = v; p.vertices.push_back(vertex);
    }
    p.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(p);
    viewer::DrawInstance draw; draw.name = "terrain"; model.draws.push_back(draw);
    model.boundsMin = {-100, 0, -100}; model.boundsMax = {100, 0, 100};
    return model;
}
void protocol() {
    using namespace multiplayer;
    Snapshot s; s.tick = 123;
    PlayerState player; player.id = 42; player.movement.feet = {12, 3, -4}; player.resolved = 99;
    player.weaponCoolingDown = true;
    player.weaponSwitchRemaining = 1.25f; player.weaponSwitchDuration = 3.5f;
    player.reloadRemaining = 2.25f; player.reloadDuration = 4.5f;
    player.ammoQuantities = {0, 5, 60};
    player.inventoryPlacements.push_back({"backpack/tactical_backpack/main", 7, 0, 90});
    s.players.push_back(player);
    s.zombies.push_back({}); s.doors.push_back({3, .5f}); s.boxes.push_back({}); s.boxes.back().removed = true;
    s.grenades.push_back({7, {1, 2, 3}, {1, 0, 0, 0}});
    s.explosions.push_back({6, {4, 5, 6}, .08f});
    const auto bytes = encode(s);
    const auto copy = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    require(copy.boxes.at(0).removed, "Snapshot round trip lost retired box state");
    require(copy.tick == 123 && copy.players[0].id == 42 && copy.players[0].movement.feet.x == 12 && copy.players[0].weaponCoolingDown,
        "Snapshot round trip lost state");
    require(copy.players[0].weaponSwitchRemaining == 1.25f && copy.players[0].weaponSwitchDuration == 3.5f,
            "Snapshot lost authoritative switch progress");
    require(copy.players[0].reloadRemaining == 2.25f && copy.players[0].reloadDuration == 4.5f &&
            copy.players[0].ammoQuantities == std::vector<std::uint32_t>({0, 5, 60}),
            "Snapshot lost reload timing or empty ammo stacks");
    require(copy.players[0].inventoryPlacements.at(0).rotation == 90 &&
            copy.players[0].inventoryPlacements[0].storage == "backpack/tactical_backpack/main",
            "Snapshot lost authoritative storage placements");
    Command inventoryCommand; inventoryCommand.sequence = 1;
    inventoryCommand.inventoryActions = {
        {viewer::InventoryDragActionKind::PickUp, "pistol_rounds_1", "", 0, 0, 0},
        {viewer::InventoryDragActionKind::Rotate, "", "", 0, 0, -1},
        {viewer::InventoryDragActionKind::Drop, "", "body/chest_pouch/pouch", 1, 0, 0},
        {viewer::InventoryDragActionKind::Cancel, "pistol_rounds_1", "", 0, 0, 0}};
    const auto commandBytes = encode(Commands{{inventoryCommand}});
    const auto inventoryCommands = std::get<Commands>(decode(commandBytes.data(), commandBytes.size()));
    require(inventoryCommands.commands[0].inventoryActions.size() == 4 &&
            inventoryCommands.commands[0].inventoryActions[0].item == "pistol_rounds_1" &&
            inventoryCommands.commands[0].inventoryActions[1].turns == 3 &&
            inventoryCommands.commands[0].inventoryActions[2].x == 1 &&
            inventoryCommands.commands[0].inventoryActions[3].item == "pistol_rounds_1",
            "Command lost inventory action order or values");
    auto invalidReload = s; invalidReload.players[0].reloadRemaining = 5;
    rejects([&] { encode(invalidReload); }, "Reload progress beyond duration accepted");
    require(copy.grenades.at(0).id == 7 && copy.grenades[0].center.y == 2 &&
            copy.explosions.at(0).id == 6 && copy.explosions[0].age == .08f,
            "Snapshot must preserve grenade poses and explosion ages");
    auto invalidGrenade = s;
    invalidGrenade.grenades[0].rotation = {0, 0, 0, 0};
    rejects([&] { encode(invalidGrenade); }, "Invalid grenade rotation accepted");
    invalidGrenade = s; invalidGrenade.explosions[0].age = -1;
    rejects([&] { encode(invalidGrenade); }, "Negative explosion age accepted");
    for (std::size_t n = 0; n < bytes.size(); ++n)
        rejects([&] { decode(bytes.data(), n); }, "Truncated message accepted");
    auto trailing = bytes; trailing.push_back(0);
    rejects([&] { decode(trailing.data(), trailing.size()); }, "Trailing data accepted");
    auto version = bytes; version[4] = 255;
    rejects([&] { decode(version.data(), version.size()); }, "Unknown protocol accepted");
    Command c; c.sequence = 1; c.slot = 4;
    auto commands = encode(Commands{{c}});
    require(std::get<Commands>(decode(commands.data(), commands.size())).commands[0].slot == 4, "Grenade slot command round trip failed");
    c.slot = static_cast<std::uint8_t>(viewer::punchSlot);
    commands = encode(Commands{{c}});
    require(std::get<Commands>(decode(commands.data(),commands.size())).commands[0].slot==viewer::punchSlot,
            "Fists slot command round trip failed");
    c.slot = static_cast<std::uint8_t>(viewer::punchSlot+1);
    rejects([&] { encode(Commands{{c}}); }, "Unavailable weapon slot accepted");
    c.slot = 4;
    c.movement.forward = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { encode(Commands{{c}}); }, "Non-finite input accepted");
    c.movement.forward = 2;
    rejects([&] { encode(Commands{{c}}); }, "Oversized movement accepted");
    s.players.push_back(player);
    rejects([&] { encode(s); }, "Duplicate entity ID accepted");
    rejects([&] { encode(Hello{"wrong"}); }, "Invalid content hash accepted");
    s.players.pop_back(); s.zombies.resize(maxZombieUpdatesPerSnapshot + 1);
    rejects([&] { encode(s); }, "Oversized entity count accepted");
}
void playerHealthProtocol() {
    using namespace multiplayer;
    Snapshot snapshot;
    PlayerState player; player.id = 1;
    snapshot.players.push_back(player);
    for (const float health : {0.f, .125f, 73.625f, 100.f}) {
        snapshot.players[0].health = health;
        const auto bytes = encode(snapshot);
        const auto copy = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
        require(copy.players.at(0).health == health, "Snapshot lost fractional health or a health boundary");
    }
    const auto valid = encode(snapshot);
    // Health follows header, tick, player count, ID and resolved command fields.
    constexpr std::size_t healthOffset = 9 + 8 + 4 + 4 + 8;
    for (const float health : {-0.01f, 100.01f, std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()}) {
        snapshot.players[0].health = health;
        rejects([&] { encode(snapshot); }, "Invalid player health encoded");
        auto malformed = valid;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &health, sizeof(bits));
        for (std::size_t i = 0; i < sizeof(bits); ++i)
            malformed[healthOffset + i] = static_cast<std::uint8_t>(bits >> (8 * i));
        rejects([&] { decode(malformed.data(), malformed.size()); }, "Invalid player health decoded");
    }
    auto oldVersion = valid; oldVersion[4] = 14;
    rejects([&] { decode(oldVersion.data(), oldVersion.size()); }, "Previous protocol accepted a health snapshot");
}
void fragments() {
    using namespace multiplayer;
    Snapshot snapshot; snapshot.tick = 100; snapshot.zombies.resize(maxZombieUpdatesPerSnapshot);
    for (std::size_t i = 0; i < snapshot.zombies.size(); ++i) snapshot.zombies[i].id = static_cast<std::uint32_t>(i);
    const auto bytes = encode(snapshot);
    require(bytes.size() > 16500, "Fragment test no longer covers GNS reliable-promotion threshold");
    std::vector<Fragment> parts;
    for (std::size_t offset = 0; offset < bytes.size(); offset += fragmentBytes) {
        Fragment f{1, static_cast<std::uint32_t>(bytes.size()), static_cast<std::uint16_t>(offset / fragmentBytes), {}};
        f.bytes.assign(bytes.begin() + offset, bytes.begin() + std::min(bytes.size(), offset + fragmentBytes));
        const auto wire = encode(f);
        require(wire.size() < 1100, "Fragment exceeds safe datagram payload");
        parts.push_back(std::get<Fragment>(decode(wire.data(), wire.size())));
    }
    Fragments buffer;
    require(!buffer.add(parts.back()), "Incomplete snapshot was published");
    require(!buffer.add(parts.back()), "Duplicate fragment completed a snapshot");
    std::optional<std::vector<std::uint8_t>> completed;
    for (std::size_t i = parts.size() - 1; i-- > 0;) completed = buffer.add(parts[i]);
    require(completed && *completed == bytes, "Reordered fragmented snapshot changed payload");
    require(!buffer.add(parts.front()), "Old fragmented snapshot was published twice");
    Fragments loss;
    for (std::size_t i = 1; i < parts.size(); ++i) loss.add(parts[i]);
    for (auto part : parts) { part.message = 2; completed = loss.add(part); }
    require(completed && *completed == bytes, "Lost snapshot blocked a newer complete snapshot");
    rejects([&] { auto bad = parts.front(); bad.message = 3; bad.totalBytes = maxMessageBytes + 1; loss.add(bad); },
        "Oversized fragmented message accepted");
}
void prediction() {
    using namespace multiplayer;
    const auto model = ground(); viewer::CollisionWorld collision(model);
    viewer::FPSController server;
    require(server.spawn({0, 1.7f, 0}, collision), "Synthetic spawn failed");
    PlayerState state; state.id = 1; state.movement = server.movementState();
    Prediction client; client.reconcile(state, collision);
    std::vector<Command> history;
    for (int i = 0; i < 60; ++i) {
        Command c; c.movement.forward = 1; c.movement.yawDegrees = 0;
        c.movement.jump = i == 10; c.movement.toggleProne = i == 30;
        history.push_back(client.advance(c, collision));
        if (i < 35) server.update(tickSeconds, c.movement, collision);
    }
    const auto expected = client.controller().movementState();
    state.resolved = 35; state.movement = server.movementState();
    client.reconcile(state, collision);
    const auto actual = client.controller().movementState();
    require(glm::length(actual.feet - expected.feet) < .0001f && actual.stance == expected.stance &&
        actual.verticalVelocity == expected.verticalVelocity, "Reconciliation lost jump/stance state");
    require(client.outstanding() == 25, "Acknowledged inputs retained");
    require(client.commandsToSend().commands.front().sequence == 36, "Resend includes resolved inputs");
    state.resolved = 100;
    rejects([&] { client.reconcile(state, collision); }, "Future acknowledgement accepted");
    for (int i = 0; i < 40; ++i) client.advance({}, collision);
    const auto batch = client.commandsToSend();
    require(batch.commands.size() == 32 && batch.commands.front().sequence == 36 && batch.commands.back().sequence == 100,
        "Bounded resend batch omitted old input or the newest input");
    const auto wire = encode(batch);
    decode(wire.data(), wire.size());
    viewer::FPSState invalid; invalid.accumulator = -1;
    rejects([&] { server.restoreMovement(invalid); }, "Invalid movement state restored");
    Snapshots buffer;
    Snapshot a; a.tick = 10;
    require(buffer.push(a, 0) && !buffer.push(a, 1), "Stale snapshot was published");
    a.tick = 9; require(!buffer.push(a, 1), "Reordered snapshot was published");
    Command localEffect;
    localEffect.sequence = client.nextSequence();
    localEffect.left = true;
    localEffect.firearmShot = FirearmShot{0, {}, true};
    localEffect.firearmShot->muzzlePosition = glm::vec3(1, 2, 3);
    const auto numbered = client.advance(localEffect, collision);
    require(numbered.sequence == localEffect.sequence, "Presentation prediction used a different command number");
    const auto retry = encode(client.commandsToSend());
    const auto decoded = std::get<Commands>(decode(retry.data(), retry.size()));
    require(decoded.commands.back().sequence == numbered.sequence &&
            decoded.commands.back().firearmShot && decoded.commands.back().firearmShot->predictedMist &&
            decoded.commands.back().firearmShot->muzzlePosition == glm::vec3(1, 2, 3),
            "Local mist or muzzle metadata was lost from the stored/retried shot command");
}
void predictionPresentation() {
    using namespace multiplayer;
    const auto model = ground(); viewer::CollisionWorld collision(model);
    viewer::FPSController server;
    require(server.spawn({10, 1.7f, 0}, collision), "Presentation spawn failed");
    PlayerState state; state.movement = server.movementState();
    Prediction client; client.reconcile(state, collision);
    const auto start = client.eye();
    require(client.presentedEye(0) == start && client.presentedEye(1) == start,
        "First spawn interpolated from an uninitialized position");

    Command walk; walk.movement.forward = 1; walk.movement.yawDegrees = 0;
    double accumulator = 0, elapsed = 0;
    // Uneven render intervals above 60 FPS must still follow a constant-speed
    // path, one command tick behind, including frames with no movement step.
    constexpr double step = 1.0 / 60;
    constexpr std::array<double, 3> frameSeconds{1.0 / 144, 1.0 / 100, 1.0 / 240};
    for (int frame = 0; frame < 60; ++frame) {
        const double dt = frameSeconds[frame % frameSeconds.size()];
        elapsed += dt; accumulator += dt;
        while (accumulator >= step) {
            client.advance(walk, collision);
            accumulator -= step;
        }
        const auto simulationEye = client.eye();
        const auto shown = client.presentedEye(static_cast<float>(accumulator / step));
        const float expectedX = start.x + 3.f * static_cast<float>(std::max(0.0, elapsed - step));
        require(std::abs(shown.x - expectedX) < .0001f,
            "Camera retained fixed-step movement jumps between render frames");
        require(client.eye() == simulationEye, "Sampling presentation moved the gameplay controller");
    }
    const auto beforeReplay = client.presentedEye(.25f);
    client.reconcile(state, collision); // All commands remain pending and replay.
    require(glm::distance(client.presentedEye(.25f), beforeReplay) < .0001f,
        "Reconciliation replay replaced the camera's interpolation history");

    state.resolved = client.nextSequence() - 1;
    state.movement = client.controller().movementState();
    state.movement.feet.x += .25f;
    const auto beforeEye = client.eye();
    const auto beforeCorrection = client.presentedEye(.25f);
    client.reconcile(state, collision);
    const auto visualOffset = beforeEye - client.eye();
    require(glm::distance(client.presentedEye(.25f) + visualOffset, beforeCorrection) < .0001f,
        "Small server correction jumped the interpolated camera before easing");

    const auto beforeCatchUp = client.eye();
    client.advance(walk, collision);
    const auto penultimateEye = client.eye();
    client.advance(walk, collision);
    require(glm::distance(client.presentedEye(0), penultimateEye) < .0001f &&
            client.presentedEye(1).x > beforeCatchUp.x,
        "Catch-up commands did not retain the two most recent predicted steps");
    require(client.presentedEye(-1) == client.presentedEye(0) &&
            client.presentedEye(2) == client.eye(), "Presentation extrapolated outside solved positions");

    state.resolved = client.nextSequence() - 1;
    state.movement = client.controller().movementState();
    state.movement.feet.x += 10;
    client.reconcile(state, collision);
    require(client.presentedEye(0) == client.eye(), "Large server correction interpolated through a teleport");

    client.advance(walk, collision);
    state.resolved = client.nextSequence() - 1;
    state.movement = client.controller().movementState();
    state.vehicle = 0;
    client.reconcile(state, collision);
    require(client.presentedEye(0) == client.eye(), "Entering a vehicle retained walking interpolation");
    const auto seatedEye = client.eye();
    client.advance(walk, collision);
    require(client.presentedEye(.5f) == seatedEye, "Seated input predicted walking movement");
    state.resolved = client.nextSequence() - 1;
    state.vehicle = noVehicle;
    state.movement.feet.x += .5f;
    client.reconcile(state, collision);
    require(client.presentedEye(0) == client.eye(), "Vehicle exit interpolated from the old seat position");
}
void physicsPoses() {
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    boxes.add({0, 1, 0}, {1, 1, 1});
    const auto revision = boxes.revision();
    boxes.setPose(0, {3, 2, 1}, {1, 0, 0, 0});
    require(boxes.pose(0).center.x == 3 && boxes.revision() > revision, "Replicated box pose not published");
    viewer::DoorPanel panel; panel.hinge = {0, 1, 0}; panel.center = {.5f, 1, 0};
    panel.halfExtents = {.5f, 1, .05f}; panel.outward = {0, 0, 1};
    viewer::DoorLayer doors({panel}, physics);
    doors.setAngle(0, .7f);
    require(std::abs(doors.angle(0) - .7f) < .001f, "Replicated door angle changed sign/frame");
    const auto hit = doors.raycast({.4f, 1, 2}, {0, 0, -1}, 4);
    require(hit.has_value(), "Replicated door disappeared from collision queries");
}
viewer::ZombieLayer crowd() {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back(); layer.setPerceptionStaggering(false);
    for (const auto feet : {glm::vec3(0), glm::vec3(200, 0, 0)}) {
        viewer::ZombieInstance instance;
        instance.transform = instance.restTransform = instance.restRoot = glm::translate(glm::mat4(1), feet);
        layer.instances.push_back(instance);
    }
    return layer;
}
void multiplayerPerception() {
    const auto model = ground();
    const viewer::CollisionWorld collision(model);
    auto layer = crowd();
    std::vector<viewer::ZombiePlayer> players{
        {{0, 0, -3}, 1.7f, 1.8f, 1}, {{0, 0, 10}, 1.7f, 1.8f, 2}, {{200, 0, 10}, 1.7f, 1.8f, 3}};
    viewer::SimulationClock::instance().reset();
    layer.updateBehaviorForPlayers(multiplayer::tickSeconds, players, collision, 0);
    require(layer.behaviors.contains(1), "Player two's area did not activate zombies");
    require(layer.behaviors.find(0)->targetPlayer == 2 && layer.behaviors.find(1)->targetPlayer == 3,
        "Zombie selected a nearer invisible player or ignored another player's area");
    require(std::abs(layer.instances[0].restRoot[3].z - .05f) < .0001f,
        "Multiple players advanced one zombie more than once");
    players[0].feet = {0, 0, 4};
    layer.updateBehaviorForPlayers(multiplayer::tickSeconds, players, collision, 0);
    require(layer.behaviors.find(0)->targetPlayer == 2, "Visible retained target switched unnecessarily");
    players.erase(players.begin() + 1);
    layer.updateBehaviorForPlayers(multiplayer::tickSeconds, players, collision, 0);
    require(layer.behaviors.find(0)->targetPlayer == 1, "Disconnected zombie target was retained");

    auto old = crowd(), single = crowd();
    const viewer::ZombiePlayer player{{0, 0, 10}, 1.7f, 1.8f, 7};
    for (int tick = 0; tick < 100; ++tick) {
        viewer::SimulationClock::instance().advance(multiplayer::tickSeconds);
        old.updateBehavior(multiplayer::tickSeconds, player, collision);
        single.updateBehaviorForPlayers(multiplayer::tickSeconds, {player}, collision, 0);
    }
    require(old.instances[0].restRoot == single.instances[0].restRoot &&
        old.behaviors.find(0)->state == single.behaviors.find(0)->state, "Single-player targeting behavior changed");
}
void occludedShooting() {
    viewer::Model model;
    viewer::Primitive wall;
    for (const glm::vec3 p : {glm::vec3(-5, 0, 5), {5, 0, 5}, {5, 3, 5}, {-5, 3, 5}}) {
        viewer::Vertex v; v.position = p; wall.vertices.push_back(v);
    }
    wall.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(wall); model.draws.push_back({});
    const viewer::CollisionWorld blocked(model), clear(viewer::Model{});
    auto layer = crowd(); layer.instances.resize(1);
    layer.instances[0].transform = layer.instances[0].restTransform = layer.instances[0].restRoot =
        glm::translate(glm::mat4(1), glm::vec3(0, 0, 10));
    layer.rebuildSpatialIndex();
    require(!layer.shootWithOcclusion({0, .9f, 0}, {0, 0, 1}, 0, blocked) && !layer.instances[0].ragdoll,
        "Server shot passed through an opaque wall");
    require(layer.shootWithOcclusion({0, .9f, 0}, {0, 0, 1}, 0, clear), "Clear server shot failed");
}
void serverCommands(const std::filesystem::path& asset) {
    using namespace multiplayer;
    Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options);
    // Use open synthetic ground so the test asset's building layout cannot decide a shot.
    scene.model = ground();
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
    scene.collision->setBoxes(scene.boxes.get()); scene.spawnEye = {0, 1.7f, 0};
    scene.zombies = crowd(); scene.zombies.instances.resize(1);
    scene.zombies.assets[0].skeleton.resize(1);
    scene.zombies.assets[0].boneFrames = {glm::mat4(1), glm::mat4(1)};
    scene.zombies.assets[0].frameCount = 2; scene.zombies.assets[0].duration = 1;
    auto& zombie = scene.zombies.instances[0];
    zombie.transform = zombie.restTransform = zombie.restRoot = glm::translate(glm::mat4(1), glm::vec3(8, 0, 0));
    scene.zombies.rebuildSpatialIndex();
    // Leave 2 HP so this command/replication fixture still exercises a lethal headshot.
    for (int hit = 0; hit < 4; ++hit)
        require(scene.zombies.shoot({0, 1.7f, 0}, {1, 0, 0}), "Replication injury setup missed");
    World world(scene);
    const auto first = world.join(), second = world.join();
    Command c; c.sequence = 1; c.left = true; c.movement.yawDegrees = 0;
    c.firearmShot = FirearmShot{0, {headReport()}};
    for (int copy = 0; copy < 5; ++copy) world.submit(first, Commands{{c}});
    for (int tick = 0; tick < 30; ++tick) world.step();
    const auto& player = world.players().at(first);
    require(player.resolved == 1 && player.shots == 1 && player.weapons.equipped().loaded == 11,
        "Duplicate commands repeated a shot or consumed extra ammo");
    Command future; future.sequence = 1000;
    rejects([&] { world.submit(first, Commands{{future}}); }, "Client bypassed command budget");
    const auto snap = test_replication::readySnapshot(world, first);
    require(snap.tick == 30 && snap.players.size() == 2, "Server snapshot omitted players");
    require(snap.zombies.size() == 1 && snap.zombies[0].dead, "Server did not replicate an authoritative kill");
    const auto pose = world.nextPose(first);
    require(pose && pose->bones.size() == 1, "Kill did not produce an independent corpse pose");
    world.poseSent(first, *pose);
    require(!world.nextPose(first), "Unacknowledged pose queued duplicate transfers");
    world.submit(first, Commands{{}, snap.tick});
    require(!world.nextPose(first), "Snapshot acknowledgement released a bulk transfer");
    rejects([&] { world.acknowledgePose(first, {pose->id, pose->version + 1}); },
        "Unsent corpse pose acknowledgement accepted");
    world.acknowledgePose(first, {pose->id, pose->version});
    require(!world.nextPose(first), "Acknowledged unchanged corpse wastes bandwidth");
    auto unchanged = world.snapshot(first);
    rejects([&] { world.submit(first, Commands{{}, snap.tick + 1}); }, "Future snapshot acknowledgement accepted");
    world.leave(second); const auto third = world.join();
    require(third != second && world.snapshot(first).players.size() == 2, "Disconnect reused a stale player identity");
    test_replication::readySnapshot(world, third);
    require(world.nextPose(third).has_value(), "Late join inherited another client's pose acknowledgement");
    Snapshots playback;
    require(playback.push(snap, 0), "Initial server snapshot rejected");
    Scene client(options); client.zombies = crowd(); client.zombies.instances.resize(1);
    client.zombies.assets[0].skeleton.resize(1);
    test_replication::installSetups(playback, client, scene);
    Snapshots reordered;
    test_replication::installSetups(reordered, client, scene);
    reordered.pushPose(client, *pose, 0); // Reliable bulk and welcome/state may arrive in either order.
    require(reordered.empty() && reordered.push(snap, 0), "Early pose blocked initial world state");
    reordered.apply(client, 0);
    require(!client.zombies.instances[0].removed && client.zombies.instances[0].ragdoll,
        "Pose received before world state was discarded");
    playback.apply(client, 0);
    require(client.zombies.instances[0].removed, "Corpse rendered before its complete pose arrived");
    playback.pushPose(client, *pose, 0);
    unchanged.tick += 3;
    require(playback.push(unchanged, .05), "New essential state rejected");
    playback.apply(client, .1);
    require(client.zombies.instances[0].ragdoll && !client.zombies.instances[0].removed &&
        client.zombies.instances[0].transform == pose->transform,
        "Late-join independent corpse pose lost its root/frame");
    auto newer = *pose; ++newer.version; newer.transform[3].y += 1;
    playback.pushPose(client, newer, .1);
    playback.pushPose(client, *pose, .1);
    playback.apply(client, .2);
    require(client.zombies.instances[0].transform == newer.transform, "Old pose overwrote newer pose");

    const auto shots = player.shots;
    for (c.sequence = 2; c.sequence <= 121; ++c.sequence) {
        reportFire(c);
        world.submit(first, Commands{{c}}); world.step();
    }
    require(player.shots == shots + 1 && player.weapons.equipped().loaded == 10,
        "Holding a semiautomatic trigger repeated shots");
    c.left = false;
    world.submit(first, Commands{{c}}); world.step(); ++c.sequence;
    c.left = true;
    world.submit(first, Commands{{c}}); world.step(); ++c.sequence;
    require(player.shots == shots + 2, "Releasing and pressing the trigger did not fire again");
    c.slot = 1;
    reportFire(c);
    const auto delay = viewer::weaponSwitchTiming(scene.initialInventory, player.weapons.equipped().config, player.weapons.state(1).config);
    const int switchTicks = static_cast<int>(std::ceil(delay->total() / tickSeconds)) + 5;
    for (int tick = 0; tick < switchTicks; ++tick, ++c.sequence) {
        world.submit(first, Commands{{c}}); world.step();
    }
    require(player.shots == shots + 2, "Switching to an automatic weapon fired the held trigger");
    c.left = false;
    world.submit(first, Commands{{c}}); world.step(); ++c.sequence;
    c.left = true;
    const auto automaticShots = player.shots;
    for (int tick = 0; tick < 60; ++tick, ++c.sequence) {
        world.submit(first, Commands{{c}}); world.step();
    }
    require(player.shots > automaticShots + 1, "Holding an automatic trigger stopped firing");
}
void replicationViewOrdering(const std::filesystem::path& asset) {
    using namespace multiplayer;
    Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options); World world(scene);
    const auto observer = world.join();
    const auto& player = world.players().at(observer);
    const auto eye = player.camera.position();
    const auto send = [&](std::uint64_t sequence, float yaw, glm::vec3 position) {
        Command command; command.sequence = sequence;
        ReplicationView view; view.eye = position; view.yaw = yaw;
        Commands commands; commands.commands.push_back(command); commands.view = view;
        world.submit(observer, commands);
    };
    send(2, 70, eye);
    require(player.replicationView && player.replicationView->yaw == 70 && player.lastViewSequence == 2,
            "A current camera view was not accepted with its command sequence");
    send(1, -120, eye);
    require(player.replicationView->yaw == 70 && player.lastViewSequence == 2,
            "A reordered command rewound the replication view");
    send(3, -30, eye + glm::vec3(100, 0, 0));
    require(player.replicationView->yaw == 70 && player.lastViewSequence == 2,
            "An out-of-range client camera changed server interest");
    send(4, 90, eye);
    require(player.replicationView->yaw == 90 && player.lastViewSequence == 4,
            "A rejected camera prevented the next valid view update");
}
void triggerResets(const std::filesystem::path& asset) {
    using namespace multiplayer;
    struct Reset { std::uint8_t slot; bool reload; };
    for (const auto reset : {Reset{0, true}, {1, true}, {1, false}, {2, false}}) {
        Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
        Scene scene(options);
        World world(scene); const auto id = world.join();
        const auto& player = world.players().at(id);
        for (int tick = 0; tick < 4; ++tick) world.step();
        std::uint64_t sequence = 0;
        const auto advance = [&](Command command, int ticks = 1) {
            for (int tick = 0; tick < ticks; ++tick) {
                command.sequence = ++sequence;
                reportFire(command);
                world.submit(id, Commands{{command}}); world.step();
            }
        };
        Command command; command.slot = reset.reload ? reset.slot : 0;
        const auto settleTicks = [&](std::size_t target) {
            const auto timing = viewer::weaponSwitchTiming(scene.initialInventory, player.weapons.equipped().config,
                                                           player.weapons.state(target).config);
            return static_cast<int>(std::ceil(timing->total() / tickSeconds)) + 5;
        };
        advance(command, settleTicks(command.slot));
        command.left = true; advance(command, 90);
        const auto shots = player.shots;
        require(shots > 0, "Trigger reset fixture must fire before reloading/switching");
        command.reload = reset.reload; command.slot = reset.slot;
        const int waitTicks = reset.reload ? 600 : settleTicks(command.slot);
        advance(command);
        command.reload = false; advance(command, std::max(90, waitTicks));
        require(player.shots == shots, "Reloading/switching fired without releasing LMB");
        // A synthesized timeout release is not a physical button release.
        for (int tick = 0; tick < 20; ++tick) world.step();
        advance(command, 90);
        require(player.shots == shots, "Input timeout cleared the reload/switch release requirement");
        command.left = false; advance(command);
        command.left = true; advance(command);
        require(player.shots == shots + 1, "A fresh press after reloading/switching did not fire");
    }
}
void catchUpWeapons(const std::filesystem::path& asset) {
    using namespace multiplayer;
    for (const bool switchWeapon : {false, true}) {
        Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
        Scene scene(options); scene.model = ground();
        scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
        scene.collision->setBoxes(scene.boxes.get()); scene.spawnEye = {0, 1.7f, 0};
        scene.zombies = crowd(); scene.zombies.instances.resize(1);
        auto& model = scene.zombies.assets.front();
        model.skeleton.resize(1); model.boneFrames = {glm::mat4(1), glm::mat4(1)};
        model.frameCount = 2; model.duration = 1;
        auto& zombie = scene.zombies.instances.front();
        zombie.transform = zombie.restTransform = zombie.restRoot = glm::translate(glm::mat4(1), glm::vec3(8, 0, 0));
        scene.zombies.rebuildSpatialIndex();
        World world(scene); const auto id = world.join();
        Commands batch;
        for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
            Command c; c.sequence = sequence; c.left = sequence == 1;
            c.movement.yawDegrees = sequence == 1 ? 0 : 180;
            c.reload = !switchWeapon && sequence == 2;
            c.slot = switchWeapon && sequence > 1 ? 1 : 0;
            if (c.left) c.firearmShot = FirearmShot{0, {headReport()}};
            batch.commands.push_back(c);
        }
        world.submit(id, batch); world.submit(id, batch);
        world.step();
        const auto& player = world.players().at(id);
        require(player.resolved == 2 && player.shots == 1,
            "Catch-up reload or weapon switch erased a preceding click");
        require(!zombie.ragdoll && scene.zombies.damage().injury(0) &&
                scene.zombies.damage().injury(0)->health == 8,
                "Catch-up shot must use its original aim and apply one low-caliber headshot");
        if (!switchWeapon) {
            require(player.weaponReload.pending() && player.weapons.equipped().loaded == 11 && player.weapons.equipped().reserve == 120,
                "Catch-up reload must defer transfer after the preceding shot");
            for (int i = 0; i < 600 && player.weaponReload.pending(); ++i) world.step();
            require(player.weapons.equipped().loaded == 12 && player.weapons.equipped().reserve == 119 &&
                    player.inventory.ammoCount("pistol_rounds") == 119,
                "Catch-up reload did not consume inventory ammo");
        }
        world.step();
        require(player.shots == 1, "Catch-up replay repeated a shot");
    }
}
void queuedInputTimeout(const std::filesystem::path& asset) {
    using namespace multiplayer;
    Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options); scene.model = ground();
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
    scene.collision->setBoxes(scene.boxes.get()); scene.spawnEye = {0, 1.7f, 0};
    World world(scene); const auto id = world.join();
    Commands batch;
    for (std::uint64_t sequence = 1; sequence <= 32; ++sequence) {
        Command c; c.sequence = sequence; c.movement.forward = 1; c.movement.yawDegrees = 0;
        c.movement.toggleProne = sequence == 29; c.left = sequence == 31; c.reload = sequence == 32;
        reportFire(c);
        batch.commands.push_back(c);
    }
    world.submit(id, batch);
    for (int tick = 0; tick < 12; ++tick) world.step();
    const auto beforeTimeout = world.players().at(id).controller.feet();
    for (int tick = 0; tick < 5; ++tick) world.step();
    const auto& player = world.players().at(id);
    require(world.snapshot(id).players.front().resolved == 32 && player.pending.empty(),
        "Queued timeout fixture did not drain all commands");
    require(player.shots == 1 && player.weaponReload.pending() && player.weapons.equipped().loaded == 11 && player.weapons.equipped().reserve == 120,
        "Packet timeout erased an acknowledged shot or pending reload");
    require(player.controller.stance() == viewer::PlayerStance::Prone &&
            glm::distance(beforeTimeout, player.controller.feet()) > .01f,
        "Packet timeout erased acknowledged movement or a prone toggle");

    // A gap is not consumable input, even when the queue is nonempty.
    Command gap; gap.sequence = 34; gap.left = true; gap.movement.toggleProne = true;
    world.submit(id, Commands{{gap}});
    for (int tick = 0; tick < 14; ++tick) world.step();
    const auto stopped = player.controller.feet();
    for (int tick = 0; tick < 30; ++tick) world.step();
    require(player.resolved == 32 && player.pending.size() == 1 && player.shots == 1 &&
            player.controller.stance() == viewer::PlayerStance::Prone &&
            glm::distance(stopped, player.controller.feet()) < .001f && player.held.movement.forward == 0,
        "Missing input did not neutralize held movement or skipped an unresolved action");
}
void serverUndergrowth(const std::filesystem::path& asset) {
    using namespace multiplayer;
    for(const bool shrubs:{false,true}) {
        Options options; options.map=asset; options.trees=options.forests=false; options.zombieCount=0;
        Scene scene(options); scene.model=ground();
        scene.collision=std::make_unique<viewer::CollisionWorld>(scene.model,std::vector<viewer::TrunkCollider>{},scene.doors.get());
        scene.collision->setBoxes(scene.boxes.get()); scene.spawnEye={0,1.7f,0};
        viewer::TreeLayer trees;
        viewer::TreeAsset shrub; shrub.model.boundsMin={-2,0,-2}; shrub.model.boundsMax={2,1,2};
        trees.assets.push_back(shrub);
        if(shrubs) {
            viewer::TreeInstance instance; instance.undergrowth=viewer::UndergrowthKind::dry;
            trees.instances.push_back(instance);
        }
        scene.undergrowth=viewer::UndergrowthIndex(trees);
        scene.zombies=crowd(); scene.zombies.instances.resize(1);
        auto& model=scene.zombies.assets.front();
        model.skeleton.resize(1); model.boneFrames={glm::mat4(1),glm::mat4(1)}; model.frameCount=2; model.duration=1;
        auto& zombie=scene.zombies.instances.front();
        // Beyond ordinary running hearing, perpendicular to the zombie's view.
        zombie.transform=zombie.restTransform=zombie.restRoot=glm::translate(glm::mat4(1),glm::vec3(40,0,0));
        scene.zombies.rebuildSpatialIndex();
        World world(scene); const auto id=world.join();
        for(std::uint64_t tick=1;tick<=20;++tick) {
            Command command; command.sequence=tick; command.movement.forward=1;
            command.movement.run=true; command.movement.yawDegrees=-90;
            world.submit(id,Commands{{command}}); world.step();
        }
        require((zombie.heardNoiseRevision>0)==shrubs,
                "Server hearing must come from authoritative shrub contact, independent of client audio");
    }
}
void serverGrenades(const std::filesystem::path& asset) {
    using namespace multiplayer;
    Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options); scene.model = ground();
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model);
    scene.spawnEye = {0, 1.7f, 0};
    World world(scene);
    const auto id = world.join();
    Command throwInput; throwInput.sequence = 1; throwInput.slot = 4;
    world.submit(id, Commands{{throwInput}});
    for (int i = 0; i < 5; ++i) world.step();
    auto state = world.snapshot(id);
    require(state.players[0].slot == 0 && state.players[0].weaponSwitchRemaining > 0 && state.grenades.empty(),
            "Weapon change must publish timed progress before equipping a grenade");
    for (int i = 0; i < 600 && world.players().at(id).weaponSwitch.pending(); ++i) world.step();
    require(world.players().at(id).weapons.slot() == 4, "Timed grenade selection did not complete");
    throwInput.sequence = 2; throwInput.left = true;
    world.submit(id, Commands{{throwInput}}); world.step();
    state = world.snapshot(id);
    require(state.grenades.size() == 1 && state.players[0].loaded == 0 && state.players[0].reserve == 5 &&
            state.players[0].reloadRemaining > 0 && state.players[0].weaponCoolingDown && world.players().at(id).shots == 1,
            "Server must consume the held grenade and start retrieving another inventory grenade");
    bool exploded = false;
    for (int i = 0; i < 200; ++i) {
        world.step(); state = world.snapshot(id);
        exploded |= !state.explosions.empty();
    }
    require(exploded && state.grenades.empty() && !state.players[0].weaponCoolingDown && world.players().at(id).shots == 1,
            "Server must detonate the grenade once without repeating a held throw");
    require(state.players[0].loaded == 1 && state.players[0].reserve == 4 &&
            world.players().at(id).inventory.ammoCount("he_grenades") == 4,
            "Grenade autoreload must retrieve exactly one matching grenade from inventory");
    Snapshots snapshots;
    Snapshot event; event.tick = 1; event.explosions.push_back({1, {2, 3, 4}, .05f});
    snapshots.push(event, 10);
    require(std::abs(snapshots.explosions(10.4).at(0).age - .45f) < .0001f &&
            snapshots.explosions(11).at(0).age > .9f,
            "Client shrub motion must age confirmed events and settle during snapshot starvation");
    require(!snapshots.explosionLights(10).empty() && snapshots.explosionLights(10.2).empty(),
            "Client explosion lights must respect event age and expire during snapshot starvation");
    Snapshots delayed;
    Snapshot moving; moving.tick = 10; moving.grenades.push_back({1, {0, 1, 0}, {1, 0, 0, 0}});
    delayed.push(moving, 20);
    moving.tick = 11; delayed.push(moving, 20);
    Snapshot detonated; detonated.tick = 12; detonated.explosions.push_back({1, {0, 1, 0}, 0});
    delayed.push(detonated, 20);
    Scene replica(options, {}, std::nullopt, SceneRole::Replica);
    delayed.apply(replica, 20);
    require(delayed.grenades().empty() && !delayed.explosionLights(20).empty(),
            "An interpolated grenade must disappear on the same frame as its confirmed explosion light");
}
void serverInventoryAmmo(const std::filesystem::path& asset) {
    using namespace multiplayer;
    using Action = viewer::InventoryDragActionKind;
    Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options); World world(scene); const auto id = world.join();
    const auto& player = world.players().at(id);
    for (int i = 0; i < 5; ++i) world.step();
    std::uint64_t sequence = 0;
    const auto submit = [&](Command command) {
        command.sequence = ++sequence;
        reportFire(command);
        world.submit(id, Commands{{command}}); world.submit(id, Commands{{command}});
        world.step();
    };
    Command move;
    move.inventoryActions = {{Action::PickUp, "pistol_rounds_1", "", 0, 0, 0},
        {Action::Drop, "", "body/chest_pouch/pouch", 1, 0, 0}};
    submit(move);
    require(player.inventoryDrag.pending() && world.snapshot(id).players[0].inventoryDragging,
            "A network inventory move must start authoritative storage access");
    for (int i = 0; i < 600 && player.inventoryDrag.pending(); ++i) world.step();
    require(!player.inventory.held() && player.inventory.locationOf("pistol_rounds_1")->placement->storage == "body/chest_pouch/pouch",
            "A replicated move must commit once, without repeating held actions");
    Command fire; fire.left = true; submit(fire);
    Command reload; reload.reload = true; submit(reload);
    require(player.weaponReload.pending() &&
            std::abs(player.weaponReload.duration() - (player.weapons.equipped().config.reloadTimeSeconds + .65)) < 1e-6,
            "Server reload must use the moved ammo stack's current pouch access time");
    for (int i = 0; i < 600 && player.weaponReload.pending(); ++i) world.step();
    require(player.inventory.quantity("pistol_rounds_1") == 59 && player.inventory.quantity("pistol_rounds_2") == 60,
            "Reload must consume the fastest matching stack after the network move");
    Command merge;
    merge.inventoryActions = {{Action::PickUp, "pistol_rounds_2", "", 0, 0, 0},
        {Action::Drop, "", "body/chest_pouch/pouch", 1, 0, 0}};
    submit(merge);
    for (int i = 0; i < 600 && player.inventoryDrag.pending(); ++i) world.step();
    require(player.inventory.quantity("pistol_rounds_1") == 60 && player.inventory.quantity("pistol_rounds_2") == 59 &&
            player.inventory.ammoCount("pistol_rounds") == 119,
            "Network stack merge must preserve the source footprint and total ammo");
    const auto snapshot = world.snapshot(id).players[0];
    require(!snapshot.inventoryDragging && snapshot.inventoryPlacements.size() == player.inventory.items().size() &&
            snapshot.ammoQuantities.size() == 11,
            "Authoritative idle inventory must publish placements and all stack quantities");
    submit(fire);
    Command outward;
    outward.inventoryActions = {{Action::PickUp, "pistol_rounds_1", "", 0, 0, 0},
        {Action::Drop, "", "backpack/tactical_backpack/main", 6, 4, 0}};
    submit(outward);
    require(bool(player.inventoryDrag.pending()), "Jitter fixture must have a pending first move");
    // The client completed its first timer before this delayed server did.
    move.inventoryActions[0].item = "pistol_rounds_1";
    move.reload = true;
    submit(move);
    require(player.pendingInventoryActions.size() == 2, "Next move must wait behind the authoritative timer");
    for (int i = 0; i < 1200 && (player.inventoryDrag.pending() || !player.pendingInventoryActions.empty()); ++i) world.step();
    require(!player.inventory.held() && player.inventory.locationOf("pistol_rounds_1")->placement->storage == "body/chest_pouch/pouch" &&
            player.pendingInventoryActions.empty(), "Network jitter lost an acknowledged inventory move");
    require(player.weaponReload.pending() && std::abs(player.weaponReload.duration() - 2.15) < 1e-6,
            "A queued reload must wait for storage moves and use their completed access time");
}
void inputRecovery(const std::filesystem::path& asset) {
    using namespace multiplayer;
    Options options; options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options); scene.model = ground();
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
    scene.collision->setBoxes(scene.boxes.get()); scene.spawnEye = {0, 1.7f, 0};
    World world(scene); const auto id = world.join();
    Prediction client; client.reconcile(world.snapshot(id).players.front(), *scene.collision);
    std::deque<Commands> transit;
    // Leave time for the actual storage access and reload after each lost burst.
    for (int frame = 0; frame < 3600; ++frame) {
        const int cycle = frame % 600;
        const int lossEnd = frame < 1800 ? 90 : 150;
        Command c; c.movement.forward = cycle < 300 ? 1 : -1; c.movement.yawDegrees = 0;
        c.movement.toggleProne = cycle == 68;
        c.left = cycle == 70; c.reload = cycle == 76;
        reportFire(c);
        client.advance(c, *scene.collision);
        transit.push_back(client.commandsToSend());
        if (transit.size() > 3) {
            if (cycle < 60 || cycle >= lossEnd) {
                world.submit(id, transit.front()); world.submit(id, transit.front());
            }
            transit.pop_front();
        }
        const auto before = world.players().at(id).controller.feet();
        world.step();
        const auto& player = world.players().at(id);
        require(glm::distance(before, player.controller.feet()) <= 3 * tickSeconds + .0001f,
            "Input catch-up advanced movement faster than real time");
        require(world.tick() == static_cast<std::uint64_t>(frame + 1), "Input catch-up advanced the world clock");
        if (frame % 3 == 2) client.reconcile(world.snapshot(id).players.front(), *scene.collision);
        if (cycle == 599) {
            require(client.outstanding() <= 8, "Recovered connection retained its input backlog");
            const auto cycles = static_cast<std::uint64_t>(frame / 600 + 1);
            require(player.shots == cycles && player.weapons.equipped().loaded == 12,
                "Input catch-up lost or repeated a click/reload");
            require(player.controller.stance() == (cycles % 2 ? viewer::PlayerStance::Prone : viewer::PlayerStance::Standing),
                "Input catch-up lost or repeated a prone toggle");
        }
    }
    const auto flood = world.join();
    Commands batch;
    for (std::uint64_t seq = 1; seq <= 64; ++seq) {
        Command c; c.sequence = seq; c.movement.forward = 1; c.movement.yawDegrees = 0; c.movement.run = true;
        batch.commands.push_back(c);
        if (batch.commands.size() == 32) { world.submit(flood, batch); batch.commands.clear(); }
    }
    const auto before = world.players().at(flood).controller.feet();
    world.step();
    require(glm::distance(before, world.players().at(flood).controller.feet()) <= 6 * tickSeconds + .0001f,
        "Flooding commands increased running speed");

    const auto gap = world.join();
    Command first; first.sequence = 1; first.movement.toggleProne = true;
    auto last = first; last.sequence = 40;
    world.submit(gap, Commands{{first, last}});
    for (int i = 0; i < 8; ++i) world.step();
    require(world.players().at(gap).resolved == 1, "Newest input hint acknowledged missing button actions");
    for (std::uint64_t seq = 2; seq < 40; ++seq) {
        Command c; c.sequence = seq; batch.commands.push_back(c);
        if (batch.commands.size() == 32) { world.submit(gap, batch); batch.commands.clear(); }
    }
    world.submit(gap, batch);
    for (int i = 0; i < 24; ++i) { world.submit(gap, Commands{}); world.step(); }
    require(world.players().at(gap).resolved == 40 && world.players().at(gap).controller.stance() == viewer::PlayerStance::Standing,
        "Filling an input gap lost ordered button actions");
}
}
int main(int argc, char** argv) {
    try {
        protocol(); playerHealthProtocol(); fragments(); prediction(); predictionPresentation(); physicsPoses(); multiplayerPerception(); occludedShooting();
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        triggerResets(argv[1]); serverCommands(argv[1]); replicationViewOrdering(argv[1]); catchUpWeapons(argv[1]); queuedInputTimeout(argv[1]); inputRecovery(argv[1]); serverGrenades(argv[1]); serverUndergrowth(argv[1]); serverInventoryAmmo(argv[1]);
        std::cout << "Multiplayer core checks passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
