#include "protocol.hpp"
#include "fragments.hpp"
#include "client/prediction.hpp"
#include "client/snapshots.hpp"
#include "server/world.hpp"
#include "zombie_animation.hpp"
#include "simulation_clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
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
    s.players.push_back(player);
    s.zombies.push_back({}); s.doors.push_back({3, .5f}); s.boxes.push_back({});
    const auto bytes = encode(s);
    const auto copy = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    require(copy.tick == 123 && copy.players[0].id == 42 && copy.players[0].movement.feet.x == 12,
        "Snapshot round trip lost state");
    for (std::size_t n = 0; n < bytes.size(); ++n)
        rejects([&] { decode(bytes.data(), n); }, "Truncated message accepted");
    auto trailing = bytes; trailing.push_back(0);
    rejects([&] { decode(trailing.data(), trailing.size()); }, "Trailing data accepted");
    auto version = bytes; version[4] = 255;
    rejects([&] { decode(version.data(), version.size()); }, "Unknown protocol accepted");
    Command c; c.sequence = 1;
    auto commands = encode(Commands{{c}});
    require(std::get<Commands>(decode(commands.data(), commands.size())).commands[0].sequence == 1, "Command round trip failed");
    c.movement.forward = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { encode(Commands{{c}}); }, "Non-finite input accepted");
    c.movement.forward = 2;
    rejects([&] { encode(Commands{{c}}); }, "Oversized movement accepted");
    s.players.push_back(player);
    rejects([&] { encode(s); }, "Duplicate entity ID accepted");
    rejects([&] { encode(Hello{"wrong"}); }, "Invalid content hash accepted");
    s.players.pop_back(); s.zombies.resize(maxVisibleZombies + 1);
    rejects([&] { encode(s); }, "Oversized entity count accepted");
}
void fragments() {
    using namespace multiplayer;
    Snapshot snapshot; snapshot.tick = 100; snapshot.zombies.resize(200);
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
    World world(scene);
    const auto first = world.join(), second = world.join();
    Command c; c.sequence = 1; c.left = true; c.movement.yawDegrees = 0;
    for (int copy = 0; copy < 5; ++copy) world.submit(first, Commands{{c}});
    for (int tick = 0; tick < 30; ++tick) world.step();
    const auto& player = world.players().at(first);
    require(player.resolved == 1 && player.shots == 1 && player.weapons.equipped().loaded == 11,
        "Duplicate commands repeated a shot or consumed extra ammo");
    Command future; future.sequence = 1000;
    rejects([&] { world.submit(first, Commands{{future}}); }, "Client bypassed command budget");
    const auto snap = world.snapshot(first);
    require(snap.tick == 30 && snap.players.size() == 2, "Server snapshot omitted players");
    require(snap.zombies.size() == 1 && snap.zombies[0].dead, "Server did not replicate an authoritative kill");
    require(snap.zombies[0].bones.size() == 1 && world.snapshot(first).zombies[0].bones.size() == 1,
        "Unacknowledged corpse palette was not repeated");
    world.submit(first, Commands{{}, snap.tick});
    auto unchanged = world.snapshot(first);
    require(unchanged.zombies[0].bones.empty(), "Acknowledged unchanged corpse palette wastes snapshot bandwidth");
    rejects([&] { world.submit(first, Commands{{}, snap.tick + 1}); }, "Future snapshot acknowledgement accepted");
    world.leave(second); const auto third = world.join();
    require(third != second && world.snapshot(first).players.size() == 2, "Disconnect reused a stale player identity");
    Snapshots playback;
    require(playback.push(snap, 0), "Initial server snapshot rejected");
    Scene client(options); client.zombies = crowd(); client.zombies.instances.resize(1);
    client.zombies.assets[0].skeleton.resize(1);
    unchanged.tick += 3;
    require(playback.push(unchanged, .05) && playback.latest().zombies[0].bones.size() == 1,
        "Skipped display frames lost an acknowledged corpse palette");
    playback.apply(client, .1);
    require(client.zombies.instances[0].ragdoll && client.zombies.instances[0].transform == snap.zombies[0].transform,
        "Late-join snapshot lost corpse pose/frame");

    const auto shots = player.shots;
    for (c.sequence = 2; c.sequence <= 121; ++c.sequence) {
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
    const auto automaticShots = player.shots;
    for (int tick = 0; tick < 60; ++tick, ++c.sequence) {
        world.submit(first, Commands{{c}}); world.step();
    }
    require(player.shots > automaticShots + 1, "Holding an automatic trigger stopped firing");
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
            batch.commands.push_back(c);
        }
        world.submit(id, batch); world.submit(id, batch);
        world.step();
        const auto& player = world.players().at(id);
        require(player.resolved == 2 && player.shots == 1,
            "Catch-up reload or weapon switch erased a preceding click");
        require(zombie.ragdoll, "Catch-up shot used the later command's aim");
        if (!switchWeapon)
            require(player.weapons.equipped().loaded == 12 && player.weapons.equipped().reserve == 119,
                "Catch-up reload did not account for the preceding shot");
        world.step();
        require(player.shots == 1, "Catch-up replay repeated a shot");
    }
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
    for (int frame = 0; frame < 1800; ++frame) {
        const int cycle = frame % 300;
        const int lossEnd = frame < 900 ? 90 : 150;
        Command c; c.movement.forward = 1; c.movement.yawDegrees = 0;
        c.movement.toggleProne = cycle == 68;
        c.left = cycle == 70; c.reload = cycle == 76;
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
        if (cycle == 299) {
            require(client.outstanding() <= 8, "Recovered connection retained its input backlog");
            const auto cycles = static_cast<std::uint64_t>(frame / 300 + 1);
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
        protocol(); fragments(); prediction(); physicsPoses(); multiplayerPerception(); occludedShooting();
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        serverCommands(argv[1]); catchUpWeapons(argv[1]); inputRecovery(argv[1]);
        std::cout << "Multiplayer core checks passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
