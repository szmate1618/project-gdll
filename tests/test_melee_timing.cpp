#include "box_layer.hpp"
#include "collision_world.hpp"
#include "fps_controller.hpp"
#include "melee_combat.hpp"
#include "physics_world.hpp"
#include "player_body.hpp"
#include "player_presentation.hpp"
#include "weapons.hpp"
#include "zombie_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
viewer::Model floorModel() {
    viewer::Model model;
    viewer::Primitive floor;
    for (const glm::vec3 point : {glm::vec3(-20, 0, -20), glm::vec3(-20, 0, 20),
                                  glm::vec3(20, 0, 20), glm::vec3(20, 0, -20)}) {
        viewer::Vertex vertex; vertex.position = point;
        floor.vertices.push_back(vertex);
    }
    floor.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(floor));
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    return model;
}
viewer::AnimatedModel zombieCube() {
    viewer::AnimatedModel asset;
    asset.model = viewer::BoxLayer::unitCube();
    asset.model.draws.emplace_back();
    for (auto& vertex : asset.model.primitives[0].vertices)
        vertex.position = vertex.position * glm::vec3(.45f, 1.8f, .32f) + glm::vec3(0, .9f, 0);
    asset.model.boundsMin = {-.225f, 0, -.16f};
    asset.model.boundsMax = {.225f, 1.8f, .16f};
    return asset;
}
struct Replay {
    std::vector<viewer::PlayerBodyPose> poses;
    std::vector<glm::vec3> eyes;
    int accepted = 0, hitStep = -1;
};
Replay replay(const std::vector<float>& frameSeconds, glm::vec2 movement, bool rightHand) {
    const auto model = floorModel();
    viewer::CollisionWorld scene(model);
    viewer::FPSController controller;
    require(controller.spawn({0, 1.7f, 0}, scene), "Timing fixture must spawn on its floor");
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody body(world, controller.config().eyeHeight);
    viewer::MeleeCombat melee(world);
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY, true);
    require(weapons.equip(viewer::punchSlot), "Timing fixture must equip fists");
    const glm::vec3 aim(0, 0, -1);
    body.follow(controller.eyePosition(), aim, true);
    if (!rightHand) {
        require(body.punch(aim), "Left-hand fixture must advance the alternating hand");
        body.follow(controller.eyePosition(), aim, false);
        body.follow(controller.eyePosition(), aim, true);
    }
    viewer::ZombieLayer zombies;
    zombies.assets.push_back(zombieCube());
    viewer::FPSInput input;
    input.right = movement.x; input.forward = movement.y; input.run = true;
    Replay result;
    int stepIndex = 0;
    for (std::size_t frame = 0; stepIndex < 90; ++frame) {
        require(frame < 1000, "Render partitions must eventually advance simulation");
        world->step(frameSeconds[frame % frameSeconds.size()], [&](float step) {
            if (stepIndex >= 90) return;
            // The command is resolved at the start of a solved step. Camera,
            // physical fist, and current target surfaces describe that instant.
            body.follow(controller.eyePosition(), aim, true);
            if (stepIndex == 12) {
                viewer::ZombieInstance target;
                const auto eye = controller.eyePosition();
                target.restRoot = target.restTransform = target.transform =
                    glm::translate(glm::mat4(1), glm::vec3(eye.x, 0, eye.z - .65f));
                zombies.instances.push_back(std::move(target));
                zombies.attachPhysics(world); zombies.rebuildSpatialIndex();
                weapons.trigger(true);
                weapons.trigger(false);
            }
            melee.prepare(body, &zombies, 0);
            if (weapons.fireAttempt(stepIndex * double(step), [&] {
                    return melee.punch(body, controller.eyePosition(), aim, scene);
                }) == viewer::WeaponFireResult::fired) ++result.accepted;
            controller.update(step, input, scene);
            body.follow(controller.eyePosition(), aim, true);
            body.beforeStep(step);
        }, [&](float) {
            if (stepIndex >= 90) return;
            melee.finish(body, &zombies, 0);
            body.afterStep();
            zombies.synchronizePhysics();
            if (result.hitStep < 0 && !zombies.instances.empty() && zombies.instances[0].ragdoll)
                result.hitStep = stepIndex;
            result.poses.push_back(body.pose());
            result.eyes.push_back(controller.eyePosition());
            ++stepIndex;
        });
    }
    return result;
}
void compare(const Replay& expected, const Replay& actual) {
    require(actual.accepted == 1 && actual.accepted == expected.accepted,
            "Every render partition must accept the same click exactly once");
    require(actual.hitStep == expected.hitStep,
            "Running punch contacts must occur on the same solved step at every render rate");
    require(actual.poses.size() == expected.poses.size(), "Replays must advance the same simulated time");
    for (std::size_t step = 0; step < actual.poses.size(); ++step) {
        require(glm::distance(actual.eyes[step], expected.eyes[step]) < .0001f,
                "Walking displacement must agree at each fixed step across render partitions");
        const auto& a = expected.poses[step];
        const auto& b = actual.poses[step];
        require(a.striking == b.striking && a.returning == b.returning,
                "Strike and recovery timing must agree across render partitions");
        for (std::size_t part = 0; part < a.parts.size(); ++part)
            for (int column = 0; column < 4; ++column)
                require(glm::length(a.parts[part][column] - b.parts[part][column]) < .0001f,
                        "Physical body poses must agree across render partitions");
    }
}
void renderPartitions() {
    const std::vector<std::vector<float>> rates{{1.f / 30}, {1.f / 60}, {1.f / 120}, {1.f / 144},
        {1.f / 240, 1.f / 48, 1.f / 90, 1.f / 30}};
    for (const glm::vec2 movement : {glm::vec2(0), glm::vec2(1, 0), glm::vec2(-1, 0),
                                    glm::vec2(0, 1), glm::vec2(0, -1)}) {
        for (const bool rightHand : {false, true}) {
            const auto expected = replay(rates[1], movement, rightHand);
            if (movement == glm::vec2(0) || movement == glm::vec2(0, 1))
                require(expected.hitStep >= 12, "Stationary and forward-running fixture punches must make physical contact");
            for (const auto& rate : rates) compare(expected, replay(rate, movement, rightHand));
        }
    }
}
void clickBetweenSteps() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody body(world, 1.7f);
    body.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY, true);
    require(weapons.equip(viewer::punchSlot), "Click fixture must equip fists");
    int steps = 0, attempts = 0, accepted = 0;
    const auto before = [&](float seconds) {
        ++steps;
        if (weapons.fireAttempt(steps * double(seconds), [&] {
                ++attempts; return body.punch({0, 0, -1});
            }) == viewer::WeaponFireResult::fired) ++accepted;
        body.beforeStep(seconds);
    };
    const auto after = [&](float) { body.afterStep(); };
    const float quarterStep = viewer::PhysicsWorld::fixedStepSeconds * .25f;
    weapons.trigger(true);
    world->step(quarterStep, before, after);
    weapons.trigger(false);
    world->step(quarterStep, before, after);
    require(steps == 0 && attempts == 0 && weapons.equipped().readyAt == 0,
            "A press and release between solves must not start or spend a punch early");
    world->step(quarterStep * 2, before, after);
    require(steps == 1 && attempts == 1 && accepted == 1,
            "A zero-step press and release must survive until the next solve");
    world->step(viewer::PhysicsWorld::fixedStepSeconds * 6, before, after);
    require(attempts == 1 && accepted == 1, "Catch-up steps must not repeat a consumed punch click");
}
void presentation() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    const float step = viewer::PhysicsWorld::fixedStepSeconds;
    require(world->interpolationAlpha() == 0, "A fresh physics world has no unsolved fraction");
    world->step(step * .25f);
    require(std::abs(world->interpolationAlpha() - .25f) < .00001f,
            "Presentation alpha must include zero-step frames");
    world->step(step * .75f);
    require(world->interpolationAlpha() < .00001f, "A solved step must consume its presentation fraction");
    world->step(.2f);
    require(world->interpolationAlpha() == 1, "Capped catch-up must clamp presentation alpha to one");

    viewer::PlayerBody body(world, 1.7f);
    const glm::vec3 initialEye(0, 1.7f, 0), nextEye(.1f, 1.7f, -.05f);
    body.follow(initialEye, {0, 0, -1}, true);
    const auto initial = body.pose();
    auto next = initial; next.eye = nextEye;
    const auto turn = glm::rotate(glm::mat4(1), glm::radians(30.f), glm::vec3(0, 1, 0));
    for (auto& part : next.parts)
        part = glm::translate(glm::mat4(1), nextEye) * turn *
            glm::translate(glm::mat4(1), -initialEye) * part;
    viewer::PlayerPresentation display;
    display.advance(initialEye, initial);
    display.advance(nextEye, next);
    require(display.currentEye() == nextEye, "Gameplay must retain the latest solved eye");
    for (const float alpha : {0.f, .25f, .5f, 1.f}) {
        const auto pose = display.pose(alpha);
        require(viewer::validPlayerBodyPose(pose), "Interpolated anatomy must preserve valid rigid transforms");
        require(glm::distance(pose.eye, display.eye(alpha)) < .00001f &&
                glm::distance(display.eye(alpha), glm::mix(initialEye, nextEye, alpha)) < .00001f,
                "Camera and body must use the same interpolation fraction");
        for (std::size_t part = 0; part < pose.parts.size(); ++part) {
            require(glm::distance(glm::vec3(pose.parts[part][3]), glm::mix(glm::vec3(initial.parts[part][3]),
                glm::vec3(next.parts[part][3]), alpha)) < .00001f,
                "Every body part must interpolate between the two solved poses");
            for (int axis = 0; axis < 3; ++axis)
                require(std::abs(glm::length(glm::vec3(pose.parts[part][axis])) -
                    glm::length(glm::vec3(initial.parts[part][axis]))) < .00001f,
                    "Body interpolation must preserve anatomical dimensions");
        }
    }
    require(display.eye(-1) == initialEye && display.eye(2) == nextEye,
            "Presentation must clamp extrapolation outside the solved pair");
    auto disabled = next; disabled.enabled = false; disabled.striking = disabled.returning = 0;
    display.advance(nextEye, disabled);
    require(!display.pose(.25f).enabled, "Unequipping must immediately suppress physical-body presentation");
    const auto equippedEye = nextEye + glm::vec3(.1f, 0, 0);
    auto equipped = next; equipped.eye = equippedEye;
    for (auto& part : equipped.parts) part[3] += glm::vec4(.1f, 0, 0, 0);
    display.advance(equippedEye, equipped);
    const auto presentedEquipped = display.pose(.25f);
    require(presentedEquipped.enabled &&
            glm::distance(presentedEquipped.eye, display.eye(.25f)) < .00001f,
            "Equipping while moving must keep the complete anatomy with the interpolated camera");
    for (std::size_t part = 0; part < equipped.parts.size(); ++part) {
        require(glm::distance(glm::vec3(presentedEquipped.parts[part][3]) - presentedEquipped.eye,
            glm::vec3(equipped.parts[part][3]) - equippedEye) < .00001f,
            "Equipping presentation must preserve every body part's eye-relative position");
        for (int axis = 0; axis < 3; ++axis)
            require(presentedEquipped.parts[part][axis] == equipped.parts[part][axis],
                    "Equipping presentation must preserve the current body's orientations and dimensions");
    }
    const auto teleportedEye = equippedEye + glm::vec3(3, 0, 0);
    auto teleported = equipped; teleported.eye = teleportedEye;
    for (auto& part : teleported.parts) part[3] += glm::vec4(3, 0, 0, 0);
    display.advance(teleportedEye, teleported);
    require(display.eye(0) == teleportedEye && display.pose(0).eye == teleportedEye,
            "Teleports must reset both presentation endpoints");
    display.reset(initialEye, initial);
    require(display.eye(.5f) == initialEye && display.pose(.5f).eye == initialEye,
            "Explicit presentation reset must discard the previous pair");
}
} // namespace
int main() {
    try {
        clickBetweenSteps();
        presentation();
        renderPartitions();
        std::cout << "Melee timing and player presentation checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
