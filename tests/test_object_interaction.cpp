#include "interior_fixture.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "object_interaction.hpp"
#include "player_interaction.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using viewer::Hand;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
viewer::Camera cameraAt(glm::vec3 target, float distance = 1.2f) {
    viewer::Camera camera;
    camera.look(0, 200);
    camera.setPosition(target - camera.direction() * distance);
    return camera;
}
viewer::Model stage(bool wall = false) {
    viewer::Model model;
    model.materials.push_back({});
    interior_fixture::quad(model, {-20, 0, -20}, {-20, 0, 20}, {20, 0, 20}, {20, 0, -20}, "terrain");
    if (wall) interior_fixture::quad(model, {.75f, 0, -5}, {.75f, 0, 5}, {.75f, 5, 5}, {.75f, 5, -5}, "wall");
    interior_fixture::bounds(model);
    return model;
}
struct Fixture {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes{physics};
    viewer::CollisionWorld world;
    viewer::PhysicsScene scene;
    viewer::ObjectInteraction input;
    viewer::Camera camera;
    explicit Fixture(bool wall = false) : model(stage(wall)), world(model),
        scene(physics, world, model.boundsMin, model.boundsMax) {
        world.setBoxes(&boxes); scene.ensureNear({-10, -1, -10}, {10, 5, 10});
    }
    void advance(int frames = 180, float dt = 1.0f / 60) {
        for (int i = 0; i < frames; ++i) {
            input.update(camera, dt); scene.prepare(boxes); physics->step(dt);
        }
    }
};
float bottom(const viewer::BoxPose& pose) {
    const auto rotation = glm::mat3_cast(pose.rotation);
    const float half = std::abs(rotation[0].y) * pose.halfExtents.x + std::abs(rotation[1].y) * pose.halfExtents.y +
                       std::abs(rotation[2].y) * pose.halfExtents.z;
    return pose.center.y - half;
}
void picking() {
    Fixture f;
    f.boxes.add({0, 1, 0}, {.4f, .4f, .4f});
    f.boxes.add({0, 1, -.5f}, {.4f, .4f, .4f});
    const auto revision = f.boxes.revision();
    const auto hit = f.boxes.raycast({0, 1, 1.2f}, {0, 0, -4}, 1);
    require(hit && hit->box == 0 && std::abs(hit->distance - 1) < 1e-5f, "Picking must normalize direction, accept reach boundary and return nearest box");
    require(f.boxes.revision() == revision, "Picking must not wake or move props");
    require(!f.boxes.raycast({0, 1, 1.201f}, {0, 0, -1}, 1), "A box beyond reach must miss");
    require(!f.boxes.raycast({}, {}, 2), "Zero ray direction must miss");
    require(!f.boxes.raycast({}, {0, 0, -1}, std::numeric_limits<float>::quiet_NaN()), "Nonfinite picking reach must miss");
    f.boxes.applyImpulse(0, {2, 1, -2}, hit->point);
    for (int i = 0; i < 12; ++i) f.physics->step(1.0f / 60);
    const auto pose = f.boxes.pose(0);
    const auto state = f.boxes.pointState(0, hit->localPoint);
    require(glm::distance(state.point, pose.center + pose.rotation * hit->localPoint) < .001f, "Grip coordinates must follow a moving, rotating body");
    const auto normal = pose.rotation * glm::vec3(0, 0, 1);
    const auto moved = f.boxes.raycast(state.point + normal, -normal, 1.1f);
    require(moved && moved->box == 0 && glm::distance(moved->point, state.point) < .002f, "Picking must use the current oriented box");
    Fixture occluded(true);
    occluded.boxes.add({1.2f, 1, 0}, {.4f, .4f, .4f});
    occluded.camera = cameraAt({1.2f, 1, 0});
    occluded.camera.setPosition({0, 1, 0}); occluded.camera.look(900, 0);
    require(!occluded.input.press(occluded.camera, occluded.boxes, occluded.world), "A wall must block the grab even when the ray hits a box");
}
glm::vec3 throwOrPush(double duration, bool held, float size = .4f, bool cancel = false) {
    Fixture f;
    f.boxes.add({0, 1, 0}, glm::vec3(size));
    f.camera = cameraAt({0, 1, 0}, size * .5f + 1);
    viewer::DoorLayer doors({}, f.physics);
    viewer::PlayerInteraction input;
    if (held) {
        require(input.press(Hand::Left, f.camera, doors, f.boxes, f.world, 1), "Left click must grab");
        require(input.heldBox() == 0, "Holding left must keep the grip attached");
    }
    input.press(Hand::Right, f.camera, doors, f.boxes, f.world, 2);
    require(input.charging() && input.heldProp().has_value() == held, "Right press charges without grabbing");
    require(glm::length(f.boxes.velocity(0)) < .001f, "Charge must wait for release before applying momentum");
    if (cancel) input.cancel();
    input.release(Hand::Right, f.camera, doors, f.boxes, f.world, 2 + duration);
    require(!input.grabbing() && !input.charging(), "Right release must detach and consume the charge");
    const auto velocity = f.boxes.velocity(0);
    input.release(Hand::Right, f.camera, doors, f.boxes, f.world, 5);
    require(glm::distance(velocity, f.boxes.velocity(0)) < .001f, "Release cannot replay a charge");
    return velocity;
}
void throws() {
    require(glm::length(throwOrPush(.05, true)) < .001f, "Quick right click must drop a held object without a boost");
    require(glm::length(throwOrPush(.19, true)) < .001f, "The grenade tap allowance must also drop held objects");
    const auto full = throwOrPush(2, true), partial = throwOrPush(1, true);
    const auto push = throwOrPush(.05, false), chargedPush = throwOrPush(2, false);
    require(full.z < -.5f && full.y > 0 && std::abs(full.x) < .001f, "Full charge must launch in the aimed direction");
    require(glm::distance(partial * 2.f, full) < .001f, "A one-second hold must add half the full-charge momentum");
    require(glm::distance(full, throwOrPush(10, true)) < .001f, "Long holds must saturate");
    require(glm::length(push) > .1f && glm::length(push) < glm::length(chargedPush), "Empty-handed click must shove, and hold must charge a stronger push");
    require(glm::distance(full, chargedPush) < .001f, "Held and unheld objects must receive the same charged momentum");
    require(glm::length(throwOrPush(1, true, .4f, true)) < .001f &&
            glm::length(throwOrPush(1, false, .4f, true)) < .001f, "Cancellation must clear held and empty-handed charges");
    Fixture light, heavy;
    light.boxes.add({0, 1, 0}, glm::vec3(.4f)); heavy.boxes.add({0, 1, 0}, glm::vec3(1));
    const float lightMass = viewer::raycastPhysicsProp(light.physics, {0, 1, 2}, {0, 0, -1}, 2)->prop.mass();
    const float heavyMass = viewer::raycastPhysicsProp(heavy.physics, {0, 1, 2}, {0, 0, -1}, 2)->prop.mass();
    require(std::abs(glm::length(full) * lightMass - std::sqrt(6.3f*6.3f + .9f*.9f)) < .01f,
            "Full charge must match the launch momentum of a 450 g standard grenade");
    require(glm::distance(full * lightMass, throwOrPush(2, true, 1) * heavyMass) < .01f,
            "The same charge must give heavier objects less speed with equal momentum");
}
void automaticThrows() {
    for (const bool held : {false, true}) for (const float size : {.4f, 1.f}) {
        Fixture f;
        f.boxes.add({0, 1, 0}, glm::vec3(size));
        f.camera = cameraAt({0, 1, 0}, size * .5f + 1);
        viewer::DoorLayer doors({}, f.physics);
        viewer::PlayerInteraction input;
        if (held) require(input.press(Hand::Left, f.camera, doors, f.boxes, f.world, 0), "Automatic throw fixture must grab");
        const auto prop = viewer::raycastPhysicsProp(f.physics, f.camera.position(), f.camera.direction(), 2)->prop;
        // Start with existing momentum to detect replacing velocity instead of adding an impulse.
        prop.applyImpulse({2, 1, 3}, prop.center());
        const auto before = f.boxes.velocity(0);
        input.press(Hand::Right, f.camera, doors, f.boxes, f.world, 10);
        input.update(f.camera, 0, doors, f.boxes, f.world, 11.999);
        require(input.charging() && glm::distance(before, f.boxes.velocity(0)) < .001f,
                "Holding must not apply throw momentum before the two-second cap");
        input.update(f.camera, 0, doors, f.boxes, f.world, 12);
        require(!input.grabbing() && !input.charging(), "Two seconds must automatically detach and consume the charge");
        const auto velocity = f.boxes.velocity(0);
        const auto momentumAdded = (velocity - before) * prop.mass();
        const auto expected = f.camera.direction() * 6.3f + glm::vec3(0, .9f, 0);
        require(glm::distance(momentumAdded, expected) < .001f,
                "Automatic throws and pushes must add the same fixed momentum at different masses");
        input.update(f.camera, 0, doors, f.boxes, f.world, 15);
        input.release(Hand::Right, f.camera, doors, f.boxes, f.world, 15);
        require(glm::distance(velocity, f.boxes.velocity(0)) < .001f,
                "Continued holding and final release must not repeat an automatic throw or push");
        input.press(Hand::Right, f.camera, doors, f.boxes, f.world, 16);
        input.cancel();
        input.update(f.camera, 0, doors, f.boxes, f.world, 18);
        require(glm::distance(velocity, f.boxes.velocity(0)) < .001f, "Cancellation must prevent automatic impulses");
    }
}
void heldGripAndLook() {
    Fixture f;
    f.boxes.add({0, 1, 0}, glm::vec3(.4f)); f.camera = cameraAt({0, 1, 0});
    viewer::DoorLayer doors({}, f.physics);
    viewer::PlayerInteraction input;
    require(input.press(Hand::Left, f.camera, doors, f.boxes, f.world, 0), "Left click must grab a prop");
    auto expected = f.camera; expected.look(100, 100);
    input.look(f.camera, 100, 100);
    require(f.camera.yawDegrees() == expected.yawDegrees() && f.camera.pitchDegrees() == expected.pitchDegrees(),
            "A held grip must preserve ordinary mouse look exactly once");
    require(input.heldBox() == 0, "Mouse movement must retain the selected object");
    const auto before = f.boxes.velocity(0);
    input.release(Hand::Left, f.camera, doors, f.boxes, f.world, .5);
    require(!input.grabbing() && glm::distance(before, f.boxes.velocity(0)) < .001f,
            "Releasing left must detach without adding a throw impulse");
    f.camera = cameraAt(f.boxes.pose(0).center);
    require(input.press(Hand::Left, f.camera, doors, f.boxes, f.world, .6), "Left must allow a fresh grab after release");
    input.press(Hand::Right, f.camera, doors, f.boxes, f.world, 1);
    input.cancel();
    input.release(Hand::Right, f.camera, doors, f.boxes, f.world, 2);
    require(!input.grabbing() && glm::distance(before, f.boxes.velocity(0)) < .001f, "Unequipping or suspending input must cancel without a throw");
}
void pushReachAndOcclusion() {
    for (const bool blocked : {false, true}) {
        Fixture f(blocked);
        f.boxes.add({1.2f, 1, 0}, glm::vec3(.4f));
        f.camera.setOrientation(0, 0); f.camera.setPosition({0, 1, 0});
        viewer::DoorLayer doors({}, f.physics);
        viewer::PlayerInteraction input;
        input.press(Hand::Right, f.camera, doors, f.boxes, f.world, 0);
        input.release(Hand::Right, f.camera, doors, f.boxes, f.world, 2);
        require(blocked ? glm::length(f.boxes.velocity(0)) < .001f : f.boxes.velocity(0).x > .5f,
                "Unheld pushes must respect wall occlusion");
    }
    Fixture f; f.boxes.add({0, 1, 0}, glm::vec3(.4f)); f.camera = cameraAt({0, 1, 0});
    viewer::DoorLayer doors({}, f.physics); viewer::PlayerInteraction input;
    input.press(Hand::Right, f.camera, doors, f.boxes, f.world, 0);
    f.camera.setPosition({0, 1, 5});
    input.release(Hand::Right, f.camera, doors, f.boxes, f.world, 1);
    require(glm::length(f.boxes.velocity(0)) < .001f, "Moving out of reach during a charge must not push a remote object");
}
float lift(float size, float dt = 1.0f / 60) {
    Fixture f;
    const glm::vec3 center(0, size * .5f + .02f, 0);
    f.boxes.add(center, glm::vec3(size)); f.camera = cameraAt(center, size * .5f + 1);
    require(f.input.press(f.camera, f.boxes, f.world), "Lift fixture must be grabbable");
    f.input.moveMouse(0, 250);
    f.advance(static_cast<int>(3 / dt), dt);
    return bottom(f.boxes.pose(0));
}
void heldMovementAndDrop() {
    Fixture f;
    f.boxes.add({0, .22f, 0}, glm::vec3(.4f)); f.camera = cameraAt({0, .22f, 0});
    const auto hit = f.boxes.raycast(f.camera.position(), f.camera.direction(), 2);
    require(hit && f.input.press(f.camera, f.boxes, f.world), "Movement fixture must be grabbable");
    f.input.moveMouse(200, 200); f.advance(120);
    auto point = f.boxes.pointState(0, hit->localPoint).point;
    require(point.x > .6f && point.y > .8f, "Mouse right/up must move the attached point sideways and upward");
    const auto raised = point;
    f.input.moveMouse(-400, -200); f.advance(120);
    point = f.boxes.pointState(0, hit->localPoint).point;
    // A picked face can rotate to the top while hanging. Verify the body's
    // ground contact, not an impossible below-ground target for that grip.
    require(point.x < -.6f && point.y < raised.y - .4f && bottom(f.boxes.pose(0)) < .04f,
            "Mouse left/down must reverse sideways movement and lower the object to its collision support");
    const float before = point.z;
    f.camera.setPosition(f.camera.position() + glm::vec3(0, 0, .5f)); f.advance(120);
    require(f.boxes.pointState(0, hit->localPoint).point.z > before + .3f, "Walking backward must pull the grip toward the camera");
    f.camera.setPosition(f.camera.position() - glm::vec3(0, 0, .8f)); f.advance(120);
    require(f.boxes.pointState(0, hit->localPoint).point.z < before - .15f, "Walking forward must push the held object forward");
    f.input.moveMouse(0, 200); f.advance(120);
    require(bottom(f.boxes.pose(0)) > .5f, "Drop fixture must be airborne while held");
    const auto velocity = f.boxes.velocity(0);
    f.input.cancel();
    require(glm::distance(velocity, f.boxes.velocity(0)) < .001f, "A release without a recent swing must preserve existing momentum without a boost");
    f.advance(120);
    require(bottom(f.boxes.pose(0)) < .04f, "Released objects must fall and settle onto the floor");
}
void liftingDraggingAndCollisions() {
    const float light = lift(.4f), heavy = lift(1);
    require(light > .5f, "A small crate must lift well clear of the ground");
    require(heavy < .06f, "Objects above lifting strength must remain supported by the floor");
    const auto slow = lift(.4f, 1.0f / 30), fast = lift(.4f, 1.0f / 120);
    require(std::abs(slow - fast) < .15f, "Holding must work across frames with multiple or zero physics steps");

    Fixture drag;
    drag.boxes.add({0, .52f, 0}, glm::vec3(1)); drag.camera = cameraAt({0, .52f, 0}, 1.5f);
    require(drag.input.press(drag.camera, drag.boxes, drag.world), "Heavy box must be grabbable for dragging");
    // Move the camera as WASD does; the fixed world grip follows the eye offset.
    for (int i = 0; i < 180; ++i) {
        drag.camera.setPosition(drag.camera.position() + glm::vec3(.01f, 0, 0));
        drag.advance(1);
    }
    require(drag.boxes.pose(0).center.x > .4f && bottom(drag.boxes.pose(0)) < .06f, "WASD movement must drag an unliftable box along the floor");
    require(drag.input.grabbing(), "Normal walking displacement must preserve the hand grip");
    drag.camera.setPosition(drag.camera.position() + glm::vec3(100, 0, 0)); drag.advance(1);
    require(!drag.input.grabbing(), "A camera teleport must break the tether without a remote throw");

    Fixture blocked(true);
    blocked.boxes.add({0, .22f, 0}, glm::vec3(.4f)); blocked.camera = cameraAt({0, .22f, 0});
    require(blocked.input.press(blocked.camera, blocked.boxes, blocked.world), "Box before a wall must be reachable");
    blocked.input.moveMouse(375, 0); blocked.advance();
    require(blocked.boxes.pose(0).center.x < .65f, "Holding must not teleport or pull a box through a solid wall");
    Fixture thrown(true);
    thrown.boxes.add({0, 1, 0}, glm::vec3(.4f));
    thrown.camera.setOrientation(0, 0); thrown.camera.setPosition({-1.2f, 1, 0});
    viewer::DoorLayer doors({}, thrown.physics); viewer::PlayerInteraction input;
    require(input.press(Hand::Left, thrown.camera, doors, thrown.boxes, thrown.world, 0), "Throw collision fixture must be reachable");
    input.press(Hand::Right, thrown.camera, doors, thrown.boxes, thrown.world, 0);
    input.release(Hand::Right, thrown.camera, doors, thrown.boxes, thrown.world, 2);
    require(thrown.boxes.velocity(0).x > .5f, "Charged throw must start toward the wall");
    thrown.advance();
    require(thrown.boxes.pose(0).center.x < .65f, "A charged throw must collide with a thin static wall");
}
void routingAndCancellation() {
    for (const bool boxInFront : {false, true}) {
        Fixture f;
        viewer::DoorPanel panel;
        panel.hinge = {-.5f, 1, 0}; panel.center = {0, 1, 0}; panel.halfExtents = {.5f, 1, .02f}; panel.outward = {0, 0, 1};
        viewer::DoorLayer doors({panel}, f.physics);
        viewer::CollisionWorld world(f.model, {}, &doors); world.setBoxes(&f.boxes);
        f.boxes.add({0, 1, boxInFront ? .65f : -.5f}, glm::vec3(.4f));
        viewer::PlayerInteraction input; f.camera = cameraAt({0, 1, 0}, 1.3f);
        require(input.press(Hand::Left, f.camera, doors, f.boxes, world, 0), "Nearest reachable target must be grabbed");
        require(boxInFront ? input.heldBox().has_value() : input.grabbedDoor().has_value(), "Left hand must select the nearer door or prop");
        auto expectedCamera = f.camera;
        expectedCamera.look(100, 100);
        input.look(f.camera, 100, 100);
        require(f.camera.yawDegrees() == expectedCamera.yawDegrees() && f.camera.pitchDegrees() == expectedCamera.pitchDegrees(),
                "Left-hand door or box grabs must preserve mouse look without applying it twice");
        const auto before = f.boxes.velocity(0);
        input.cancel();
        require(!input.grabbing() && glm::distance(before, f.boxes.velocity(0)) < .001f,
                "Mode changes must cancel door and object grips without throwing");
    }
}
} // namespace
int main() {
    try {
        picking(); throws(); automaticThrows(); heldGripAndLook(); pushReachAndOcclusion(); heldMovementAndDrop(); liftingDraggingAndCollisions(); routingAndCancellation();
        std::cout << "Object picking, hands, throw directions, strength, movement and collision tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
