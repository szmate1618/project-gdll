#include "fps_controller.hpp"
#include "camera_rig.hpp"
#include "zombie_layer.hpp"
#include "zombie_perception.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
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
    const auto box = player.collisionBox();
    const auto contacts = box ? world.contacts(*box) :
        world.contacts(player.feet(), player.config().radius, player.bodyHeight());
    for (const auto& c : contacts)
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
    require(player.stance() == PlayerStance::Prone && std::abs(player.bodyHeight() - .45f) < 1e-5f &&
            std::abs(player.eyeHeight() - .35f) < 1e-5f, "One prone press must lower the body and camera");
    require(player.collisionBox().has_value(), "Prone must use the separate box collider");
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

void lowProneClearance() {
    for (const float ceiling : {.40f, .50f}) {
        auto model = floor();
        quad(model, {0, ceiling, -3}, {3, ceiling, -3}, {3, ceiling, 3}, {0, ceiling, 3});
        const viewer::CollisionWorld world(model);
        auto player = spawn(world, {-1, 1.7f, 0});
        viewer::FPSInput input;
        input.yawDegrees = 0; input.toggleProne = true;
        tick(player, world, input);
        input.toggleProne = false; input.forward = 1;
        run(player, world, 3, input);
        require(player.stance() == PlayerStance::Prone,
                "A low tunnel must retain the crawling stance");
        if (ceiling > .45f)
            require(player.feet().x > 1.f, "The 45 cm prone box must pass beneath a 50 cm ceiling");
        else
            require(player.feet().x < -.19f, "A 40 cm ceiling must block the prone head and torso");
        clear(player, world);
    }

    for (const float width : {.55f, .65f}) {
        auto model = floor();
        for (const float side : {-width * .5f, width * .5f})
            quad(model, {0, 0, side}, {3, 0, side}, {3, 2, side}, {0, 2, side});
        const viewer::CollisionWorld world(model);
        auto player = spawn(world, {-1, 1.7f, 0});
        viewer::FPSInput input;
        input.yawDegrees = 0; input.toggleProne = true;
        tick(player, world, input);
        input.toggleProne = false; input.forward = 1;
        run(player, world, 3, input);
        if (width > .60f)
            require(player.feet().x > 1.f, "A 65 cm gap must admit the prone shoulders");
        else
            require(player.feet().x < -.19f, "Lower prone height must preserve 60 cm shoulder clearance");
        clear(player, world);
    }
}

void proneTorsoClearance() {
    auto model = floor();
    quad(model, {-.5f, 0, -3}, {-.5f, 2, -3}, {-.5f, 2, 3}, {-.5f, 0, 3});
    const viewer::CollisionWorld blockedWorld(model);
    auto blocked = spawn(blockedWorld);
    viewer::FPSInput input;
    input.yawDegrees = 0;
    tick(blocked, blockedWorld, input);
    const auto before = blocked.feet();
    const auto eye = blocked.eyePosition();
    input.toggleProne = true;
    tick(blocked, blockedWorld, input);
    require(blocked.stance() == PlayerStance::Standing && !blocked.collisionBox() &&
            glm::length(blocked.feet() - before) < 1e-5f &&
            glm::length(blocked.eyePosition() - eye) < 1e-5f,
            "Going prone must wait when the torso box would intersect a wall behind the head");
    clear(blocked, blockedWorld);

    model = floor();
    quad(model, {-1, 0, -3}, {-1, 2, -3}, {-1, 2, 3}, {-1, 0, 3});
    const viewer::CollisionWorld world(model);
    auto player = spawn(world);
    input = {}; input.yawDegrees = 0; input.toggleProne = true;
    tick(player, world, input);
    input.toggleProne = false; input.forward = -1;
    run(player, world, 1, input);
    require(player.stance() == PlayerStance::Prone && player.feet().x > -.23f && player.feet().x < -.15f,
            "Backing up prone must collide at the rear of the torso rather than at the camera");
    clear(player, world);
}

void proneTurningAndRestore() {
    auto model = floor();
    for (const float side : {-.5f, .5f})
        quad(model, {-2, 0, side}, {2, 0, side}, {2, 2, side}, {-2, 2, side});
    const viewer::CollisionWorld world(model);
    auto player = spawn(world);
    viewer::FPSInput input;
    input.yawDegrees = 0; input.toggleProne = true;
    tick(player, world, input);
    const auto feet = player.feet();
    input.toggleProne = false; input.yawDegrees = 180;
    run(player, world, .25f, input);
    require(std::abs(std::remainder(player.movementState().bodyYawDegrees, 360.f)) < 90.f &&
            glm::length(player.feet() - feet) < .001f,
            "A rapid turn must sweep the torso corners; clear final orientation alone is insufficient");
    clear(player, world);

    viewer::FPSController restored(player.config());
    restored.restoreMovement(player.movementState());
    const auto originalBox = player.collisionBox(), restoredBox = restored.collisionBox();
    require(originalBox && restoredBox && glm::length(originalBox->center - restoredBox->center) < 1e-6f &&
            std::abs(glm::dot(originalBox->rotation, restoredBox->rotation)) > .99999f,
            "Reconciliation must preserve the collision-constrained prone body orientation");
    input.yawDegrees = 0; input.forward = 1;
    run(player, world, 5, input);
    run(restored, world, 5, input);
    require(glm::length(player.feet() - restored.feet()) < 1e-5f &&
            std::abs(player.movementState().bodyYawDegrees - restored.movementState().bodyYawDegrees) < 1e-5f,
            "Restored prone movement must replay identically");
    input.forward = 0; input.yawDegrees = 180;
    tick(player, world, input);
    require(std::abs(std::abs(std::remainder(player.movementState().bodyYawDegrees, 360.f)) - 180.f) < .01f,
            "The prone body must turn freely after leaving the wall");
    clear(player, world);

    auto invalid = player.movementState();
    invalid.bodyYawDegrees = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try { restored.restoreMovement(invalid); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Movement restore must reject a nonfinite box orientation");
}

void proneSlopeSupport() {
    viewer::Model model;
    constexpr float slope = .15f;
    quad(model, {-30, -30 * slope, -30}, {-30, -30 * slope, 30},
         {30, 30 * slope, 30}, {30, 30 * slope, -30}, "terrain");
    const viewer::CollisionWorld world(model);
    auto player = spawn(world);
    viewer::FPSInput input;
    input.yawDegrees = 0; input.toggleProne = true;
    tick(player, world, input);
    require(player.stance() == PlayerStance::Prone && player.grounded(),
            "Going prone on a walkable slope must settle the whole box onto ground support");
    clear(player, world);
    input.toggleProne = false; input.forward = 1;
    run(player, world, 1, input);
    require(player.feet().x > .70f && player.grounded(),
            "The prone box must crawl up a gentle slope without catching its front edge");
    clear(player, world);
    input.forward = 0;
    run(player, world, .1f, input);
    const auto settled = player.feet();
    run(player, world, 2, input);
    require(player.grounded() && glm::length(player.feet() - settled) < .002f,
            "A supported prone box must not drift down a slope while idle");
    clear(player, world);
    input.yawDegrees = 180;
    run(player, world, .25f, input);
    require(std::abs(std::abs(std::remainder(player.movementState().bodyYawDegrees, 360.f)) - 180.f) < .01f &&
            glm::length(glm::vec2(player.feet().x - settled.x, player.feet().z - settled.z)) < .001f &&
            player.grounded(),
            "On an open slope the prone box must turn with support adjustment and no horizontal camera shift");
    clear(player, world);
}

void proneStepsAndEdges() {
    for (const bool ceiling : {false, true}) {
        auto model = floor();
        constexpr float rise = .18f;
        quad(model, {0, 0, -3}, {0, rise, -3}, {0, rise, 3}, {0, 0, 3});
        quad(model, {0, rise, -3}, {0, rise, 3}, {3, rise, 3}, {3, rise, -3});
        if (ceiling)
            quad(model, {-.6f, .55f, -3}, {3, .55f, -3}, {3, .55f, 3}, {-.6f, .55f, 3});
        const viewer::CollisionWorld world(model);
        auto player = spawn(world, {-1, 1.7f, 0});
        viewer::FPSInput input;
        input.yawDegrees = 0; input.toggleProne = true;
        tick(player, world, input);
        input.toggleProne = false; input.forward = 1;
        run(player, world, 3, input);
        if (ceiling)
            require(player.feet().x < -.19f && player.feet().y < .01f,
                    "Prone stepping must reject a lift that would push the torso through a ceiling");
        else
            require(player.feet().x > 1.f && player.feet().y > rise - .005f && player.grounded(),
                    "The prone box must step onto a low ledge and retain ground support");
        clear(player, world);
    }

    auto model = floor();
    quad(model, {-3, 1, -3}, {-3, 1, 3}, {0, 1, 3}, {0, 1, -3});
    const viewer::CollisionWorld world(model);
    viewer::FPSController player;
    require(player.startFalling({-.5f, 2.702f, 0}, world), "Platform fixture must start above the raised support");
    run(player, world, .1f);
    require(player.grounded() && std::abs(player.feet().x + .5f) < .001f &&
            std::abs(player.feet().y - 1.f) < .01f,
            "Platform fixture must settle on the raised platform before testing its edge");
    viewer::FPSInput input;
    input.yawDegrees = 0; input.toggleProne = true;
    tick(player, world, input);
    input.toggleProne = false; input.forward = 1;
    run(player, world, 1, input);
    require(player.feet().x > .2f && player.feet().y > .99f && player.grounded(),
            "The prone torso must retain platform support after the head passes its edge");
    bool airborne = false;
    for (int frame = 0; frame < 240; ++frame) {
        tick(player, world, input);
        airborne = airborne || !player.grounded();
        clear(player, world);
    }
    require(airborne && player.grounded() && player.feet().y < .01f &&
            player.stance() == PlayerStance::Prone,
            "Leaving the entire platform must make the prone body fall and land on the lower floor");
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
        lowProneClearance();
        proneTorsoClearance();
        proneTurningAndRestore();
        proneSlopeSupport();
        proneStepsAndEdges();
        clearHigherObstacles();
        cameraAndPerception();
        std::cout << "Player crouch, prone, airborne tuck, clearance and perception tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Player stance failed: " << e.what() << '\n';
        return 1;
    }
}
