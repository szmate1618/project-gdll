#include "zombie_attack.hpp"
#include "zombie_layer.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void panel(viewer::Model& model, float bottom, float top, const std::string& name = "building_test_walls",
           float left = -5, float right = 5, bool detail = false, bool low = false) {
    viewer::Primitive primitive;
    for (const auto point : {glm::vec3(left, bottom, .5f), glm::vec3(right, bottom, .5f),
                            glm::vec3(right, top, .5f), glm::vec3(left, top, .5f)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        vertex.normal = {0, 0, 1};
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name, detail ? 120.f : 0, low,
                           detail ? "building_test" : ""});
    model.primitives.push_back(std::move(primitive));
}

viewer::ZombieLayer pursuer() {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    layer.instances.emplace_back();
    auto& behavior = layer.behaviors.create(0, viewer::SimulationClock::instance().seconds());
    behavior.state = viewer::ZombieState::chasing;
    behavior.target = {0, 0, 1};
    behavior.sight = viewer::ZombieSight::full;
    // Isolate attack checks from normal sight, including a stale clear cache.
    behavior.perception.initialized = true;
    behavior.perception.period = .1;
    behavior.perception.nextSeconds = 1000;
    return layer;
}

void exposedBodyPointsAndRange() {
    const viewer::ZombieInstance instance;
    const viewer::ZombiePlayer player{{0, 0, 1}};
    for (int exposed = 0; exposed < 4; ++exposed) {
        viewer::Model model;
        // At the halfway wall plane the eye rays meet y=1.675, 1.275, .825.
        if (exposed == 0) panel(model, 0, 1.55f); // Only head.
        if (exposed == 1) { panel(model, 0, 1.1f); panel(model, 1.5f, 3); } // Only middle.
        if (exposed == 2) panel(model, 1.05f, 3); // Only feet.
        if (exposed == 3) panel(model, 0, 3); // None.
        const viewer::CollisionWorld world(model);
        std::size_t rays = 0;
        require(viewer::canZombieAttack(instance, player, world, rays) == (exposed < 3),
                "Any exposed head/middle/feet point must allow attack; a solid wall must prevent it");
        require(rays == std::size_t(std::min(exposed + 1, 3)), "Attack rays must stop on the first clear point");
    }
    const viewer::CollisionWorld clear(viewer::Model{});
    std::size_t rays = 0;
    require(viewer::canZombieAttack(instance, {{0, 0, 1.5f}}, clear, rays) && rays == 1,
            "The 1.5 m boundary must remain eligible");
    for (const auto feet : {glm::vec3(0, 0, 1.501f), glm::vec3(0, 2, 0), glm::vec3(0, 0, -1)}) {
        rays = 0;
        require(!viewer::canZombieAttack(instance, {feet}, clear, rays) && rays == 0,
                "Out-of-range, other-floor and behind-the-zombie players must fail before raycasts");
    }
    auto dead = instance;
    dead.ragdoll = true;
    rays = 0;
    require(!viewer::canZombieAttack(dead, player, clear, rays) && rays == 0,
            "A queued hit must not survive ragdoll handoff");
    dead.ragdoll = false;
    dead.removed = true;
    require(!viewer::canZombieAttack(dead, player, clear, rays) && rays == 0,
            "A removed zombie cannot validate a hit");
}

void stateCadenceAndFreshHitChecks() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    viewer::Model model;
    panel(model, 0, 3);
    const viewer::CollisionWorld blocked(model);
    auto layer = pursuer();
    layer.behaviors.find(0)->lunge.nextAttackSeconds = 1000; // Isolate eligibility from physical attack motion.
    layer.chase(.001f, {0, 0, 1}, clear);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::attacking &&
            layer.behaviorStats().attackChecks == 1 && layer.behaviorStats().attackRays == 1,
            "Attack range entry must receive one immediate fresh check");
    clock.advance(.02);
    layer.chase(.001f, {0, 0, 1}, blocked);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::attacking &&
            layer.behaviorStats().attackChecks == 0 && layer.behaviorStats().attackRays == 0,
            "Attack state may retain eligibility only until its next 10 Hz slot");
    std::size_t rays = 0;
    require(!viewer::canZombieAttack(layer.instances[0], {{0, 0, 1}}, blocked, rays) && rays == 3 &&
            layer.behaviors.find(0)->attackVisible,
            "A hit-time check must reject cover immediately despite cached attack eligibility");
    clock.advance(.08);
    layer.chase(.001f, {.2f, 0, 1}, blocked);
    require(layer.behaviors.find(0)->state != viewer::ZombieState::attacking &&
            layer.behaviorStats().attackChecks == 1 && layer.behaviorStats().attackRays == 3,
            "The next 10 Hz check must leave attack state behind cover");
    require(layer.behaviors.find(0)->sight == viewer::ZombieSight::none &&
            layer.behaviors.find(0)->target == glm::vec3(0, 0, 1) &&
            layer.behaviors.find(0)->state != viewer::ZombieState::idle,
            "Blocked attack sight must invalidate cached clear sight without leaking the hidden player's new position");
    clock.advance(.02);
    layer.chase(.001f, {0, 0, 1}, clear);
    require(layer.behaviors.find(0)->state != viewer::ZombieState::attacking && layer.behaviorStats().attackChecks == 0,
            "Opening cover between slots must not trigger every-frame state checks");
    clock.advance(.08);
    layer.chase(.001f, {0, 0, 1}, clear);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::attacking && layer.behaviorStats().attackRays == 1,
            "An unobstructed player must become attackable at the next slot");
    clock.advance(.01);
    layer.chase(.001f, {0, 0, 3}, clear);
    require(layer.behaviors.find(0)->state != viewer::ZombieState::attacking && layer.behaviorStats().attackRays == 0,
            "Leaving attack range must stop attacking without extra raycasts");
    layer.chase(.001f, {0, 0, 1}, blocked);
    require(layer.behaviors.find(0)->state != viewer::ZombieState::attacking && layer.behaviorStats().attackChecks == 1,
            "Range reentry must not reuse an old clear attack result");

    // Exercise the second attack transition after movement enters range.
    clock.reset();
    auto approaching = pursuer();
    approaching.behaviors.find(0)->target = {0, 0, 2};
    viewer::Model fartherModel;
    panel(fartherModel, 0, 3);
    fartherModel.draws[0].transform = glm::translate(glm::mat4(1), {0, 0, .5f});
    const viewer::CollisionWorld fartherWall(fartherModel);
    approaching.chase(.2f, {0, 0, 2}, fartherWall);
    require(approaching.behaviorStats().attackChecks == 1 && approaching.behaviorStats().attackRays == 3 &&
            approaching.behaviors.find(0)->state != viewer::ZombieState::attacking &&
            approaching.behaviors.find(0)->state != viewer::ZombieState::idle,
            "Movement into range must validate visibility and retain the last target when cover blocks attack");
}

void rateAndStaggering() {
    auto& clock = viewer::SimulationClock::instance();
    const viewer::CollisionWorld clear(viewer::Model{});
    for (const int fps : {30, 60, 144}) {
        clock.reset();
        auto layer = pursuer();
        layer.behaviors.find(0)->lunge.nextAttackSeconds = 1000; // Measure visibility scheduling, not hit rays.
        std::size_t checks = 0, rays = 0;
        for (int step = 0; step <= fps; ++step) {
            if (step) clock.advance(1.0 / fps);
            layer.chase(1.f / fps, {0, 0, 1}, clear);
            checks += layer.behaviorStats().attackChecks;
            rays += layer.behaviorStats().attackRays;
            require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviorStats().attackChecks <= 1,
                    "Attack checks must not depend on the ordinary perception clock or run twice in a step");
        }
        require(checks == 11 && rays == 11, "One second must contain ten scheduled checks plus immediate range entry");
    }
    viewer::ZombiePerceptionClock a, b, replay;
    require(a.dueEvery(0, .1, 0) && b.dueEvery(0, .1, 1) && replay.dueEvery(0, .1, 1),
            "Each ID receives an immediate first observation");
    require(a.nextSeconds != b.nextSeconds && b.nextSeconds == replay.nextSeconds,
            "Fixed attack slots must be staggered deterministically by stable ID");
    require(a.dueEvery(5, .1, 0) && !a.dueEvery(5, .1, 0), "Missed checks must not burst or repeat at the same time");
}

void liveDoorPose() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    viewer::Model model;
    panel(model, 0, 3, "building_test_walls", -5, 5, true, true);
    panel(model, 0, 3, "building_test_detailed_walls", -5, -.5f, true);
    panel(model, 0, 3, "building_test_detailed_walls", .5f, 5, true);
    panel(model, 2.1f, 3, "building_test_detailed_walls", -.5f, .5f, true);
    panel(model, 0, 2.1f, "building_test_doors", -.5f, .5f, true);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    const viewer::CollisionWorld world(model, {}, &doors);
    const auto drive = [&](bool open) {
        const auto& p = doors.panel(0);
        const auto edge = p.center + glm::cross(glm::vec3(0, 1, 0), p.outward) * (p.halfExtents.x * .9f);
        for (int step = 0; step < 180; ++step) {
            const auto point = glm::vec3(doors.transform(0) * glm::inverse(p.closedTransform) * glm::vec4(edge, 1));
            doors.applyForce(0, glm::cross(glm::vec3(0, open ? -1 : 1, 0), point - p.hinge) * 100.f, point);
            physics->step(1.f / 60);
            clock.advance(1.0 / 60);
        }
    };
    drive(true);
    auto layer = pursuer();
    layer.behaviors.find(0)->lunge.nextAttackSeconds = 1000; // This fixture exercises live door eligibility.
    layer.chase(.001f, {0, 0, 1}, world);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::attacking, "An open door must allow attack");
    drive(false);
    std::size_t rays = 0;
    require(!viewer::canZombieAttack(layer.instances[0], {{0, 0, 1}}, world, rays) &&
            layer.behaviors.find(0)->attackVisible, "Fresh hit checks must read the current closed panel, ignoring cached attack state");
    layer.chase(.001f, {0, 0, 1}, world);
    require(layer.behaviors.find(0)->state != viewer::ZombieState::attacking,
            "A due state check must leave attack state behind the physical door");
    drive(true);
    layer.chase(.001f, {0, 0, 1}, world);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::attacking, "Reopening the same door must restore eligibility");
}
}

int main() {
    try {
        exposedBodyPointsAndRange();
        stateCadenceAndFreshHitChecks();
        rateAndStaggering();
        liveDoorPose();
        std::cout << "Zombie attack visibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie attack test failed: " << error.what() << '\n';
        return 1;
    }
}
