#include "zombie_shooting_fixture.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_movement.hpp"
#include "zombie_door_collision.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include "simulation_clock.hpp"
#include "box_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void quad(viewer::Model& model, const std::string& name, glm::vec3 a, glm::vec3 b,
          glm::vec3 c, glm::vec3 d, bool detail = false, bool low = false) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        vertex.normal = glm::normalize(glm::cross(b - a, c - a));
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name, detail ? 120.f : 0, low,
                           detail ? "building_test" : ""});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model doorway(bool outward = true) {
    viewer::Model model;
    quad(model, "terrain", {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30});
    const auto panel = [&](float left, float bottom, float right, float top, const std::string& suffix, bool low = false) {
        quad(model, "building_test_" + suffix, {left, bottom, 0}, {right, bottom, 0},
             {right, top, 0}, {left, top, 0}, true, low);
    };
    panel(-10, 0, 10, 3, "walls", true);
    panel(-10, 0, -.5f, 3, "detailed_walls");
    panel(.5f, 0, 10, 3, "detailed_walls");
    panel(-.5f, 2.1f, .5f, 3, "detailed_walls");
    if (outward) panel(-.5f, 0, .5f, 2.1f, "doors");
    else quad(model, "building_test_doors", {.5f, 0, 0}, {-.5f, 0, 0},
              {-.5f, 2.1f, 0}, {.5f, 2.1f, 0}, true);
    return model;
}
void drive(viewer::DoorLayer& doors, viewer::PhysicsWorld& physics, bool open, float dt,
           bool playerImpulse = false) {
    const auto& p = doors.panel(0);
    const auto edge = p.center + glm::cross(glm::vec3(0, 1, 0), p.outward) * (p.halfExtents.x * .9f);
    const auto point = glm::vec3(doors.transform(0) * glm::inverse(p.closedTransform) * glm::vec4(edge, 1));
    const auto tangent = glm::cross(glm::vec3(0, open ? -1 : 1, 0), point - p.hinge);
    if (playerImpulse) doors.applyImpulse(0, glm::normalize(tangent) * (180.f * dt), point); // DoorInteraction's force cap.
    else doors.applyForce(0, tangent * 100.f, point);
    physics.step(dt);
}
float overlap(glm::vec3 feet, const viewer::CollisionWorld& world) {
    float depth = 0;
    for (const auto& c : world.contacts(feet, .3f, 1.8f, 0, .05f))
        if (!c.terrainSupport && c.normal.y < .7f) depth = std::max(depth, c.depth);
    return depth;
}

std::vector<float> followingDoor(const std::filesystem::path& trace, bool outward) {
    constexpr float dt = 1.f / 60;
    viewer::SimulationClock::instance().reset();
    auto model = doorway(outward);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    const viewer::CollisionWorld world(model, {}, &doors);
    for (int step = 0; step < 180; ++step) drive(doors, *physics, true, dt);
    viewer::ZombieLayer layer;
    layer.attachPhysics(physics);
    layer.assets.emplace_back();
    layer.assets[0].duration = 2; layer.assets[0].frameCount = 60;
    layer.assets[0].runClip = viewer::BakedAnimationClip{"running", 1, 30, 60, {}};
    for (std::size_t id = 0; id < 6; ++id) {
        viewer::ZombieInstance instance;
        const glm::vec3 feet{.08f, 0, .4f + float(id) * .72f};
        instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), glm::radians(180.f), {0, 1, 0});
        instance.transform = instance.restTransform = instance.restRoot;
        layer.instances.push_back(instance);
        auto& behavior = layer.behaviors.create(id, 0, feet);
        behavior.state = viewer::ZombieState::chasing;
        behavior.target = {0, 0, -12};
    }
    layer.rebuildSpatialIndex();
    viewer::ZombieDoorCollision blockers(physics);
    std::ofstream log;
    if (!trace.empty()) {
        std::filesystem::create_directories(trace);
        log.open(trace / (outward ? "following-outward.csv" : "following-inward.csv"));
        require(bool(log), "Cannot write door trace");
        log << std::setprecision(10) << "step,id,door_angle,x,y,z,yaw_rad,walking,clip,frame,overlap_before,overlap_after,displacement,attack_phase\n";
    }
    std::size_t penetrations = 0, latePenetrations = 0;
    float maxOverlap = 0;
    std::vector<float> replay;
    for (int step = 0; step < 900; ++step) {
        blockers.prepare(doors, layer);
        drive(doors, *physics, step < 8 || step >= 128, dt);
        replay.push_back(doors.angle(0));
        std::vector<glm::vec3> previous;
        std::vector<float> before;
        std::vector<viewer::ZombieLunge::Phase> attackBefore;
        for (std::size_t id = 0; id < layer.instances.size(); ++id) {
            const auto& instance = layer.instances[id];
            previous.emplace_back(instance.restRoot[3]);
            before.push_back(overlap(previous.back(), world));
            const auto* behavior = layer.behaviors.find(id);
            attackBefore.push_back(behavior ? behavior->lunge.phase : viewer::ZombieLunge::Phase::ready);
        }
        viewer::SimulationClock::instance().advance(dt);
        layer.updateBehavior(dt, {{0, 0, -12}}, world, (step + 1) * double(dt));
        for (std::size_t id = 0; id < layer.instances.size(); ++id) {
            const auto& instance = layer.instances[id];
            const auto feet = glm::vec3(instance.restRoot[3]);
            replay.insert(replay.end(), {feet.x, feet.y, feet.z});
            const auto depth = overlap(feet, world);
            penetrations += depth > .001f;
            latePenetrations += step > 240 && depth > .001f;
            maxOverlap = std::max(maxOverlap, depth);
            const auto* behavior = layer.behaviors.find(id);
            const auto attackPhase = behavior ? behavior->lunge.phase : viewer::ZombieLunge::Phase::ready;
            const bool attackMovement = attackBefore[id] != viewer::ZombieLunge::Phase::ready ||
                attackPhase != viewer::ZombieLunge::Phase::ready;
            const float maximumTravel = attackMovement ? viewer::zombieLungeSpeed * dt + .0002f : .0502f;
            require(glm::length(feet - previous[id]) <= maximumTravel,
                    "Door collision must not teleport or push zombies through the world");
            for (std::size_t other = 0; other < id; ++other)
                require(glm::length(feet - glm::vec3(layer.instances[other].restRoot[3])) >= .5998f,
                        "Door interaction must retain zombie body separation");
            const auto* clip = viewer::zombieAnimationClip(layer.assets[0], instance);
            const auto frame = viewer::animatedFrame(clip ? clip->duration : layer.assets[0].duration,
                clip ? clip->frameCount : layer.assets[0].frameCount,
                clip ? instance.walkSeconds : (step + 1) * double(dt), instance.phase);
            if (log) log << step << ',' << id << ',' << doors.angle(0) << ',' << feet.x << ',' << feet.y << ','
                << feet.z << ',' << std::atan2(instance.restRoot[2].x, instance.restRoot[2].z) << ','
                << instance.walking << ',' << (clip ? 2 : 0) << ',' << frame
                << ',' << before[id] << ',' << depth << ',' << glm::length(feet - previous[id]) << ','
                << int(attackPhase) << '\n';
        }
    }
    std::size_t entered = 0;
    for (const auto& instance : layer.instances) entered += instance.restRoot[3].z < -1;
    std::cout << "Following " << (outward ? "outward" : "inward") << " door: entered=" << entered << "/6, overlapping frames=" << penetrations
              << ", late overlaps=" << latePenetrations << ", max overlap=" << maxOverlap << '\n';
    require(entered == 6, "All following zombies must enter after the door closes and reopens");
    require(penetrations == 0, "Door physics must prevent zombie overlap throughout closing and reopening");
    require(latePenetrations == 0, "Reopened door must not leave zombies embedded in its panel or jamb");
    require(blockers.count() == 0, "Walking away from the door must release all six proxies");
    return replay;
}

void idleBlockerAndSelection() {
    for (const int fps : {10, 30, 60, 144}) {
        auto model = doorway();
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        const viewer::CollisionWorld world(model, {}, &doors);
        viewer::ZombieLayer layer;
        layer.attachPhysics(physics);
        layer.assets.emplace_back();
        layer.instances.resize(10002);
        for (std::size_t id = 0; id < layer.instances.size(); ++id) {
            const glm::vec3 feet = id == 0 ? glm::vec3(0, 0, .5f) : id == 1 ? glm::vec3(0, 10, .5f) :
                glm::vec3(100 + float(id % 100), 0, 100 + float(id / 100));
            layer.instances[id].restRoot = layer.instances[id].transform = layer.instances[id].restTransform =
                glm::translate(glm::mat4(1), feet);
        }
        layer.rebuildSpatialIndex();
        viewer::ZombieDoorCollision blockers(physics);
        blockers.prepare(doors, layer);
        require(blockers.count() == 0, "Inactive doors must not allocate zombie physics bodies");
        doors.activateNear({0, 0, 0});
        blockers.prepare(doors, layer);
        require(blockers.count() == 1 && layer.behaviors.entries().empty(),
                "Door-local selection must include idle zombies but exclude other floors and the rest of the city");
        const float dt = 1.f / fps;
        for (int step = 0; step < 3 * fps; ++step) {
            blockers.prepare(doors, layer);
            drive(doors, *physics, true, dt, true);
            const auto depth = overlap({0, 0, .5f}, world);
            if (depth >= .001f) std::cerr << "Opening contact: fps=" << fps << ", step=" << step
                << ", angle=" << doors.angle(0) << ", depth=" << depth << '\n';
            require(depth < .001f, "Opening pressure must not drive a panel into a stationary zombie");
        }
        require(doors.angle(0) < .5f && doors.angle(0) > .05f && layer.instances[0].restRoot[3].z == .5f,
                "The existing solver must limit an opening attempt without moving the zombie");
        // Other prop types must not gain zombie collision as a side effect.
        viewer::BoxLayer boxes(physics);
        boxes.add({.2f, .9f, .5f}, {.2f, .2f, .2f});
        for (int step = 0; step < fps / 4; ++step) physics->step(dt);
        require(std::abs(boxes.pose(0).center.x - .2f) < .0001f && std::abs(boxes.pose(0).center.z - .5f) < .0001f,
                "Door-only zombie proxies must not collide with interior boxes");
        // Moving away must update/remove the old proxy before physics runs.
        layer.instances[0].restRoot[3].z = 4;
        layer.instances[0].transform = layer.instances[0].restTransform = layer.instances[0].restRoot;
        layer.rebuildSpatialIndex();
        blockers.prepare(doors, layer);
        require(blockers.count() == 0, "Leaving all door sweeps must release the proxy");
        for (int step = 0; step < 3 * fps; ++step) drive(doors, *physics, true, dt, true);
        require(doors.angle(0) > 1.5f, "The same opening force must work after the zombie moves away");
        layer.instances[0].restRoot[3].z = .5f;
        layer.instances[0].transform = layer.instances[0].restTransform = layer.instances[0].restRoot;
        layer.rebuildSpatialIndex();
        blockers.prepare(doors, layer);
        require(blockers.count() == 1, "Returning to a doorway must recreate its proxy");
        require(viewer::test::shootToKill(layer, {2, .9f, .5f}, {-1, 0, 0}), "The door obstruction must remain shootable");
        blockers.prepare(doors, layer);
        require(blockers.count() == 0, "Ragdoll handoff must remove the living proxy before the next physics step");
        layer.removeRagdoll(0);
    }
}
}
int main(int argc, char** argv) {
    try {
        const std::filesystem::path trace = argc > 1 ? argv[1] : "";
        idleBlockerAndSelection();
        for (const bool outward : {true, false}) {
            const auto first = followingDoor(trace, outward);
            require(first == followingDoor({}, outward), "Identical door/crowd steps must replay deterministically");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Zombie door collision failed: " << e.what() << '\n';
        return 1;
    }
}
