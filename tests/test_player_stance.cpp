#include "fps_controller.hpp"
#include "camera_rig.hpp"
#include "zombie_layer.hpp"
#include "zombie_perception.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using viewer::PlayerStance;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void quad(viewer::Model& model, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const char* name = "obstacle") {
    viewer::Primitive primitive;
    for (const auto position : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = position;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model floor() {
    viewer::Model model;
    quad(model, {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30}, "terrain");
    return model;
}
viewer::FPSController spawn(const viewer::CollisionWorld& world, glm::vec3 eye = {0, 1.7f, 0}) {
    viewer::FPSController player;
    require(player.spawn(eye, world), "Stance fixture must spawn");
    return player;
}
void tick(viewer::FPSController& player, const viewer::CollisionWorld& world, viewer::FPSInput input = {}) {
    player.update(1.f / 120, input, world);
}
void run(viewer::FPSController& player, const viewer::CollisionWorld& world, float seconds, viewer::FPSInput input = {}) {
    for (int step = 0; step < int(std::lround(seconds * 120)); ++step) tick(player, world, input);
}
void clear(const viewer::FPSController& player, const viewer::CollisionWorld& world) {
    for (const auto& c : world.contacts(player.feet(), player.config().radius, player.bodyHeight()))
        require(c.depth <= .0022f, "Stance body must not penetrate the scene");
}

void groundStances() {
    const viewer::CollisionWorld world(floor());
    auto player = spawn(world);
    const auto feet = player.feet();
    const auto eye = player.eyePosition();
    viewer::FPSInput input;
    input.crouch = true;
    tick(player, world, input);
    require(player.stance() == PlayerStance::Crouched && std::abs(player.bodyHeight() - 1.1f) < 1e-5f &&
            glm::length(player.feet() - feet) < 1e-5f && std::abs(eye.y - player.eyePosition().y - .7f) < 1e-5f,
            "Grounded Ctrl must crouch around planted feet and lower the camera");
    input.forward = 1; input.yawDegrees = 0; input.run = true;
    run(player, world, 1, input);
    require(std::abs(player.feet().x - 1.5f) < .01f, "Crouching must use its slower speed even with Shift held");
    input = {};
    tick(player, world, input);
    require(player.stance() == PlayerStance::Standing, "Releasing Ctrl must stand when clear");
    input.toggleProne = true;
    tick(player, world, input);
    require(player.stance() == PlayerStance::Prone && std::abs(player.bodyHeight() - .6f) < 1e-5f &&
            std::abs(player.eyeHeight() - .5f) < 1e-5f, "One prone press must lower the body and camera");
    input.toggleProne = false; input.forward = 1; input.yawDegrees = 0; input.run = true; input.crouch = true;
    const float startX = player.feet().x;
    run(player, world, 1, input);
    require(player.stance() == PlayerStance::Prone && std::abs(player.feet().x - startX - .75f) < .01f,
            "Prone must persist without holding the key; Ctrl/Shift must not accidentally stand or sprint");
    input.toggleProne = true;
    tick(player, world, input);
    require(player.stance() == PlayerStance::Crouched, "Toggling prone off with Ctrl held must return to crouch");
    input = {}; input.toggleProne = true;
    tick(player, world, input);
    input = {}; input.jump = true;
    tick(player, world, input);
    require(player.stance() == PlayerStance::Standing && player.grounded(), "Space must get up from prone first");
    tick(player, world);
    tick(player, world, input);
    require(!player.grounded() && player.verticalVelocity() > 3, "A subsequent Space press must jump");
}

void clearanceAndQueuedInput() {
    auto model = floor();
    quad(model, {0, 1.3f, -3}, {5, 1.3f, -3}, {5, 1.3f, 3}, {0, 1.3f, 3});
    const viewer::CollisionWorld world(model);
    auto player = spawn(world, {-1, 1.7f, 0});
    viewer::FPSInput input;
    input.forward = 1; input.yawDegrees = 0; input.crouch = true;
    run(player, world, 1.1f, input);
    require(player.feet().x > .5f, "Crouching must fit under a low ceiling");
    input.crouch = false;
    tick(player, world, input);
    require(player.stance() == PlayerStance::Crouched, "A ceiling must prevent standing after Ctrl release");
    clear(player, world);
    run(player, world, 4, input);
    require(player.feet().x > 5.4f && player.stance() == PlayerStance::Standing,
            "The pending taller stance must succeed after leaving low cover");

    auto queued = spawn(world, {-3, 1.7f, 0});
    input = {}; input.toggleProne = true;
    queued.update(.001f, input, world);
    require(queued.stance() == PlayerStance::Standing, "Sub-timestep input must wait for a fixed step");
    run(queued, world, .02f);
    require(queued.stance() == PlayerStance::Prone, "A short prone press must survive until a physics step");
    queued.teleport({-3, .002f, 0});
    run(queued, world, .02f);
    require(queued.stance() == PlayerStance::Standing, "Placement must reset stance and pending toggles");
}

void airborneTuck() {
    const viewer::CollisionWorld world(floor());
    auto normal = spawn(world), tucked = spawn(world);
    viewer::FPSInput input;
    input.forward = 1; input.yawDegrees = 0; input.run = true; input.jump = true;
    for (int step = 0; step < 85; ++step) {
        tick(normal, world, input);
        auto tuckInput = input;
        tuckInput.crouch = step >= 12 && step < 60;
        tick(tucked, world, tuckInput);
        require(glm::length(normal.eyePosition() - tucked.eyePosition()) < .0002f &&
                std::abs(normal.verticalVelocity() - tucked.verticalVelocity()) < .0001f,
                "Tucking/releasing must preserve the camera arc, horizontal speed and jump velocity");
        if (tuckInput.crouch)
            require(std::abs(tucked.feet().y - normal.feet().y - .7f) < .0002f,
                    "A tuck must lift the collision feet while keeping the head fixed");
        clear(tucked, world);
    }
    require(tucked.stance() == PlayerStance::Standing, "Legs must extend when released in clear air");
    auto queued = spawn(world);
    input = {}; input.jump = true;
    tick(queued, world, input);
    input.jump = false; input.toggleProne = true;
    tick(queued, world, input);
    require(queued.stance() == PlayerStance::Standing, "Prone in midair must wait for landing");
    run(queued, world, 1.5f);
    require(queued.grounded() && queued.stance() == PlayerStance::Prone, "Queued prone must apply on landing");
}

void proneTunnel() {
    auto model = floor();
    quad(model, {0, .75f, -3}, {2, .75f, -3}, {2, .75f, 3}, {0, .75f, 3});
    const viewer::CollisionWorld world(model);
    auto player = spawn(world, {-1, 1.7f, 0});
    viewer::FPSInput input;
    input.forward = 1; input.yawDegrees = 0; input.crouch = true;
    run(player, world, 1, input);
    require(player.feet().x < 0, "Crouching must remain blocked by a prone-height tunnel");
    input.crouch = false; input.toggleProne = true;
    tick(player, world, input);
    input.toggleProne = false;
    run(player, world, 1, input);
    require(player.feet().x > .4f && player.stance() == PlayerStance::Prone,
            "Prone must crawl beneath cover too low for crouching");
    input.toggleProne = true;
    tick(player, world, input);
    input.toggleProne = false;
    require(player.stance() == PlayerStance::Prone, "Low cover must prevent both standing and crouching");
    run(player, world, 3, input);
    clear(player, world);
    require(player.feet().x > 2.4f && player.stance() == PlayerStance::Standing,
            "A blocked request to leave prone must complete when space becomes available");
}

void clearHigherObstacles() {
    auto model = floor();
    quad(model, {0, 0, -3}, {0, 1.15f, -3}, {0, 1.15f, 3}, {0, 0, 3});
    quad(model, {0, 1.15f, -3}, {0, 1.15f, 3}, {8, 1.15f, 3}, {8, 1.15f, -3});
    const viewer::CollisionWorld world(model);
    glm::vec3 first{0};
    for (const int fps : {30, 60, 144}) {
        auto normal = spawn(world, {-2, 1.7f, 0}), tucked = spawn(world, {-2, 1.7f, 0});
        viewer::FPSInput input;
        input.forward = 1; input.yawDegrees = 0; input.run = true; input.jump = true;
        bool heldAboveObstacle = false;
        for (int frame = 0; frame < int(std::lround(.9f * fps)); ++frame) {
            normal.update(1.f / fps, input, world);
            auto tuckInput = input;
            tuckInput.crouch = float(frame) / fps >= .1f;
            // Release while the legs would extend into the platform. The
            // controller must retain the tuck until landing permits standing.
            if (tucked.feet().x > .5f && !tucked.grounded()) {
                tuckInput.crouch = false;
                heldAboveObstacle = true;
            }
            tucked.update(1.f / fps, tuckInput, world);
            if (heldAboveObstacle && !tucked.grounded())
                require(tucked.stance() == PlayerStance::Crouched, "Leg extension into an obstacle must remain blocked");
            clear(tucked, world);
        }
        require(normal.feet().x < 0 && tucked.feet().x > .8f && tucked.feet().y > 1.14f && tucked.grounded(),
                "A tucked jump must clear and land on a 1.15 m platform that blocks a normal jump");
        require(heldAboveObstacle, "Fixture must exercise blocked airborne leg extension");
        if (fps == 30) first = tucked.feet();
        else require(glm::length(first - tucked.feet()) < .08f, "Tucked traversal must remain stable across render rates");
    }
}

void cameraAndPerception() {
    auto model = floor();
    quad(model, {-5, 0, 1}, {5, 0, 1}, {5, 1.4f, 1}, {-5, 1.4f, 1});
    const viewer::CollisionWorld world(model);
    viewer::CameraRig rig(world, {-30, 0, -30}, {30, 3, 30}, 1);
    require(rig.enterWalking(glm::vec3(0, 1.7f, 2)), "Rig must spawn behind cover");
    const viewer::ZombieInstance zombie;
    const auto sight = [&] {
        const auto& player = rig.player();
        const viewer::ZombiePlayer target{player.feet(), player.eyeHeight(), player.bodyHeight()};
        require(glm::length(target.feet + glm::vec3(0, target.eyeHeight, 0) - rig.camera().position()) < 1e-5f,
                "Perception head target must coincide with the actual stance camera");
        std::size_t rays = 0;
        return viewer::sampleZombieSight(zombie, target, world, rays);
    };
    require(sight() == viewer::ZombieSight::partial, "Standing head must be visible above low cover");
    rig.update(1.f / 120, 0, 0, 0, false, false, true);
    require(sight() == viewer::ZombieSight::none && rig.player().stance() == PlayerStance::Crouched,
            "Crouch must lower all player perception targets behind cover");
    rig.update(1.f / 120, 0, 0, 0, false, false, false, true);
    require(sight() == viewer::ZombieSight::none && rig.player().stance() == PlayerStance::Prone,
            "Prone must expose its actual reduced dimensions to perception");
    rig.enterFreeFly();
    const auto eye = rig.camera().position();
    rig.update(.1f, 0, 0, 0, false, false, true, true);
    require(rig.camera().position() == eye, "Stance controls must not affect free-fly");
}
}
int main() {
    try {
        groundStances();
        clearanceAndQueuedInput();
        airborneTuck();
        proneTunnel();
        clearHigherObstacles();
        cameraAndPerception();
        std::cout << "Player crouch, prone, airborne tuck, clearance and perception tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Player stance failed: " << e.what() << '\n';
        return 1;
    }
}
