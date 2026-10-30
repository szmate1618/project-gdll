#include "server/world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void openScene(Scene& scene, bool perception = true) {
    viewer::Model model;
    viewer::Primitive ground;
    for (const glm::vec3 point : {glm::vec3(-100, 0, -100), {-100, 0, 100},
                                  {100, 0, 100}, {100, 0, -100}}) {
        viewer::Vertex vertex;
        vertex.position = point;
        ground.vertices.push_back(vertex);
    }
    ground.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(ground));
    viewer::DrawInstance draw;
    draw.name = "terrain";
    model.draws.push_back(draw);
    model.boundsMin = {-100, 0, -100};
    model.boundsMax = {100, 8, 100};
    scene.model = std::move(model);
    scene.doors = std::make_unique<viewer::DoorLayer>(std::vector<viewer::DoorPanel>{}, scene.physics);
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
    scene.collision->setBoxes(scene.boxes.get());
    scene.collision->setVehicles(scene.vehicles.get());
    scene.spawnEye = {0, 1.7f, 0};
    scene.spawnOrientation = {-90, 0};
    scene.vehicles->add({60, .68f, 60}); // Prevent a fallback car near the attack fixture.

    // A tiny authored left/right swing exercises the baked-animation contact
    // path without depending on downloaded meshes. Local +Z faces the player.
    auto& asset = scene.zombies.assets.emplace_back();
    asset.skeleton.resize(2);
    asset.skeleton[0].name = "bip L Hand"; asset.skeleton[1].name = "bip R Hand";
    for (bool right : {false, true}) {
        viewer::BakedAnimationClip clip;
        clip.duration = 1; clip.frameCount = 5; clip.loop = false;
        for (auto point : {glm::vec3(-.4f, 1, -.1f), {-.4f, 1, 0}, {0, 1, 1.1f},
                           {.4f, 1, .1f}, {-.4f, 1, -.1f}}) {
            if (right) point.x = -point.x;
            clip.boneFrames.push_back(glm::translate(glm::mat4(1), right ? glm::vec3(0) : point));
            clip.boneFrames.push_back(glm::translate(glm::mat4(1), right ? point : glm::vec3(0)));
        }
        (right ? asset.attackRightClip : asset.attackLeftClip) = clip;
    }
    viewer::ZombieInstance zombie;
    zombie.restRoot = zombie.restTransform = zombie.transform =
        glm::translate(glm::mat4(1), glm::vec3(0, 0, -1));
    scene.zombies.instances.push_back(zombie);
    scene.zombies.setPerceptionEnabled(perception);
    scene.zombies.rebuildSpatialIndex();
}

Snapshot wire(const Snapshot& snapshot) {
    const auto bytes = encode(snapshot);
    return std::get<Snapshot>(decode(bytes.data(), bytes.size()));
}

const PlayerState& playerState(const Snapshot& snapshot, std::uint32_t id) {
    const auto found = std::find_if(snapshot.players.begin(), snapshot.players.end(),
        [&](const auto& player) { return player.id == id; });
    require(found != snapshot.players.end(), "Snapshot omitted a joined player");
    return *found;
}

void escapedRagdollIsRetiredByServer(const Options& options) {
    Scene scene(options);
    openScene(scene,false);
    World world(scene);
    require(scene.zombies.knockDown(0,{300,0,0},0),"Server escape fixture must activate");
    for (int tick=0;tick<600 && !scene.zombies.instances[0].removed;++tick) world.step();
    require(scene.zombies.instances[0].removed && scene.zombies.ragdollCount()==0,
            "Authoritative server must retire an escaped ragdoll using its loaded scene height");
}

void stationaryHitsAndIndependentHealth(const Options& options) {
    Scene scene(options);
    openScene(scene);
    World world(scene);
    const auto target = world.join(), other = world.join();
    require(world.players().at(target).vitals.health() == 100.f &&
            world.players().at(other).vitals.health() == 100.f,
            "Each joined player must start with 100 health");

    int firstHit = -1;
    for (int tick = 0; tick < 180; ++tick) {
        world.step();
        if (world.players().at(target).vitals.health() < 100.f) {
            firstHit = tick;
            break;
        }
    }
    require(firstHit >= 0, "A nearby stationary player must receive a zombie attack");
    require(std::abs(world.players().at(target).vitals.health() - 90.f) < .0001f,
            "A stationary player must take one base-damage hit, including contact-clipped lunges");
    require(world.players().at(other).vitals.health() == 100.f,
            "A hit must affect only the player whose body was contacted");

    for (int tick = 0; tick < 10; ++tick) {
        world.step();
        require(std::abs(world.players().at(target).vitals.health() - 90.f) < .0001f,
                "Contact and recovery must not repeat damage within one attack");
    }
    bool repeated = false;
    for (int tick = 0; tick < 180; ++tick) {
        world.step();
        if (world.players().at(target).vitals.health() < 90.f - .0001f) {
            repeated = true;
            break;
        }
    }
    require(repeated && std::abs(world.players().at(target).vitals.health() - 80.f) < .0001f,
            "A recovered zombie must eventually attack again for exactly one further base hit");
    require(world.players().at(other).vitals.health() == 100.f,
            "Repeated attacks must preserve the other player's independent health");

    const auto snapshot = wire(world.snapshot(other));
    require(playerState(snapshot, target).health == world.players().at(target).vitals.health() &&
            std::abs(playerState(snapshot, target).health - 80.f) < .0001f &&
            playerState(snapshot, other).health == 100.f,
            "Authoritative snapshots must preserve the real attack outcome and independently owned health");
}

void disabledPerceptionStopsDamage(const Options& options) {
    Scene scene(options);
    openScene(scene, false);
    World world(scene);
    const auto player = world.join();
    for (int tick = 0; tick < 180; ++tick) {
        world.step();
        require(world.players().at(player).vitals.health() == 100.f && scene.zombies.attackHits().empty(),
                "Disabling perception must suppress attack creation and damage events");
    }
    scene.zombies.setPerceptionEnabled(true);
    for (int tick = 0; tick < 180 && world.players().at(player).vitals.health() == 100.f; ++tick)
        world.step();
    const float health = world.players().at(player).vitals.health();
    require(health < 100.f, "Reenabling perception must restore attacking");
    scene.zombies.setPerceptionEnabled(false);
    for (int tick = 0; tick < 180; ++tick) {
        world.step();
        require(world.players().at(player).vitals.health() == health && scene.zombies.attackHits().empty(),
                "Disabling an active attacker must not leave queued or repeated hits");
    }
}

void backlogVelocityMeasuresTheWholeTick(const Options& options) {
    Scene scene(options);
    openScene(scene, false);
    World world(scene);
    const auto player = world.join();
    world.step();
    world.step();
    const auto& beforePlayer = world.players().at(player);
    const auto before = beforePlayer.controller.feet();
    Command right;
    right.sequence = 1;
    right.slot = static_cast<std::uint8_t>(beforePlayer.weapons.slot());
    right.movement.yawDegrees = -90;
    right.movement.right = 1;
    right.movement.run = true;
    auto forward = right;
    forward.sequence = 2;
    forward.movement.right = 0;
    forward.movement.forward = 1;
    auto third = right;
    third.sequence = 3;
    auto fourth = right;
    fourth.sequence = 4;
    world.submit(player, Commands{{right, forward, third, fourth}});
    world.step();
    const auto& after = world.players().at(player);
    require(after.resolved == 2, "The velocity fixture must consume two queued commands in one server tick");
    const auto measured = (after.controller.feet() - before) / tickSeconds;
    require(measured.x > 2.f && measured.z < -2.f,
            "The backlog fixture must move in both input directions during the tick");
    require(glm::distance(after.velocity, measured) < .0001f,
            "Attack damage must use full-tick player velocity rather than only the last input substep");
    require(glm::length(glm::vec2(after.velocity.x, after.velocity.z)) <= after.controller.config().runSpeed + .0001f,
            "Draining queued inputs must not inflate player velocity beyond actual running speed");
}

void vehicleExitDoesNotBecomeImpactVelocity(const Options& options) {
    Scene scene(options);
    openScene(scene, false);
    const auto parked = scene.vehicles->pose(0).chassis;
    auto entry = scene.vehicles->config(0).seats[1].entryPosition;
    entry.x += entry.x < 0 ? -.6f : .6f;
    scene.spawnEye = parked.center + parked.rotation * entry;
    World world(scene);
    const auto player = world.join();
    Command enter;
    enter.sequence = 1;
    enter.slot = static_cast<std::uint8_t>(world.players().at(player).weapons.slot());
    enter.enterVehicle = true;
    world.submit(player, Commands{{enter}});
    for (int tick = 0; tick < 8 && world.players().at(player).resolved < 1; ++tick)
        world.step();
    const auto seated = world.snapshot(player);
    require(playerState(seated, player).vehicle == 0 && playerState(seated, player).seat == 1,
            "The exit-velocity fixture must occupy the parked passenger seat");
    require(glm::distance(world.players().at(player).velocity, scene.vehicles->velocity(0)) < .0001f,
            "Seated player velocity must describe the vehicle rather than the entry teleport");

    const auto before = world.players().at(player).controller.feet();
    enter.sequence = 2;
    world.submit(player, Commands{{enter}});
    world.step();
    const auto exited = world.snapshot(player);
    const auto& after = world.players().at(player);
    require(after.resolved == 2 && playerState(exited, player).vehicle == noVehicle,
            "A fresh Enter command must exit the parked car");
    require(glm::distance(after.controller.feet(), before) > .5f,
            "The exit fixture must actually reposition the player away from the seat");
    require(glm::length(after.velocity) < .05f,
            "A stationary vehicle exit must not turn seat repositioning into damaging relative velocity");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test map");
        Options options;
        options.map = argv[1];
        options.zombieCount = 0;
        options.trees = options.forests = false;
        stationaryHitsAndIndependentHealth(options);
        escapedRagdollIsRetiredByServer(options);
        disabledPerceptionStopsDamage(options);
        backlogVelocityMeasuresTheWholeTick(options);
        vehicleExitDoesNotBecomeImpactVelocity(options);
        std::cout << "Multiplayer zombie attack authority, health and measured velocity passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Multiplayer zombie attack test failed: " << error.what() << '\n';
        return 1;
    }
}
