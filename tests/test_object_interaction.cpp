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
    require(!occluded.input.press(Hand::Left, occluded.camera, occluded.boxes, occluded.world, 0), "A wall must block the grab even when the ray hits a box");
}
glm::vec3 releaseVelocity(glm::vec2 motion, float size = .4f, double delay = .02, bool cancel = false) {
    Fixture f;
    f.boxes.add({0, 1, 0}, glm::vec3(size));
    f.camera = cameraAt({0, 1, 0}, size * .5f + 1);
    require(f.input.press(Hand::Left, f.camera, f.boxes, f.world, 1), "Release fixture must grab the box");
    f.input.moveMouse(motion.x, motion.y, 1.01);
    if (cancel) f.input.cancel(); else f.input.release(Hand::Left, 1.01 + delay);
    require(!f.input.grabbing(), "Released or cancelled hand must detach");
    return f.boxes.velocity(0);
}
void throws() {
    const auto up = releaseVelocity({0, 120});
    const auto right = releaseVelocity({120, 0}), left = releaseVelocity({-120, 0});
    const auto diagonal = releaseVelocity({60, 60}), down = releaseVelocity({0, -120});
    require(up.y > 4 && up.z < -2 && std::abs(up.x) < .001f, "Upward release must add both up and forward momentum");
    require(right.x > 4 && std::abs(right.z) < .001f && std::abs(right.y) < .001f && left.x < -4 && std::abs(left.z) < .001f,
            "Perfect sideways releases must add no forward or vertical momentum");
    require(diagonal.x > 2 && diagonal.y > 2 && diagonal.z < -1 && diagonal.z > up.z && std::abs(diagonal.z / diagonal.y + .6f) < .001f,
            "Upward diagonal releases must interpolate the forward component");
    require(down.y < -4 && std::abs(down.z) < .001f, "Downward release must add no forward momentum");
    require(glm::length(releaseVelocity({0, 120}, .4f, .2)) < .001f, "An old stopped swing must expire into an ordinary drop");
    require(glm::length(releaseVelocity({120, 120}, .4f, .02, true)) < .001f, "Focus/mode cancellation must not turn recent motion into a throw");
    const auto heavy = releaseVelocity({120, 0}, 1);
    require(heavy.x > .5f && heavy.x < right.x * .4f, "The same throw gesture must impart less speed to a heavy object");
    require(releaseVelocity({0, 120}, 1).y <= .001f, "An unliftable box must not gain upward throw momentum");
    const auto rotated = viewer::ObjectInteraction::throwVelocity({0, 5}, {0, 0, 1}, {1, 0, 0});
    require(glm::distance(rotated, glm::vec3(3, 5, 0)) < .001f, "Throw boost must follow camera yaw rather than a fixed world direction");
}
void independentHandsAndLook() {
    Fixture f;
    f.boxes.add({-1, 1, 0}, glm::vec3(.4f)); f.boxes.add({1, 1, 0}, glm::vec3(.4f));
    viewer::DoorLayer doors({}, f.physics);
    viewer::PlayerInteraction interaction;
    f.camera = cameraAt({-1, 1, 0});
    require(!interaction.press(Hand::Left, f.camera, doors, f.boxes, f.world, 0), "Combat clicks must not grab props");
    interaction.toggleMode();
    require(interaction.press(Hand::Left, f.camera, doors, f.boxes, f.world, 1), "Left hand must grab a prop");
    f.camera.setPosition({1, 1, 1.2f});
    require(interaction.press(Hand::Right, f.camera, doors, f.boxes, f.world, 1), "Right hand must grab a second prop without dropping the first");
    const auto yaw = f.camera.yawDegrees(), pitch = f.camera.pitchDegrees();
    interaction.look(f.camera, 100, 100, 1.01);
    require(f.camera.yawDegrees() == yaw && f.camera.pitchDegrees() == pitch, "Either held hand must freeze camera look");
    require(interaction.heldBox(Hand::Left) == 0 && interaction.heldBox(Hand::Right) == 1, "Hands must retain separate box identities");
    interaction.release(Hand::Left, 1.02);
    require(!interaction.heldBox(Hand::Left) && interaction.heldBox(Hand::Right) == 1 && f.boxes.velocity(0).x > 0 && glm::length(f.boxes.velocity(1)) < .001f,
            "Releasing one hand must throw only its object and leave the other grip unchanged");
    interaction.look(f.camera, 50, 50, 1.03);
    require(f.camera.yawDegrees() == yaw && f.camera.pitchDegrees() == pitch, "Right hand alone must keep the camera fixed");
    interaction.cancel();
    require(!interaction.grabbing(), "Cancellation must drop both hands");
    interaction.look(f.camera, 50, 0, 1.05);
    require(f.camera.yawDegrees() != yaw, "Normal camera look must resume after the last grip ends");

    Fixture shared;
    shared.boxes.add({0, 1, 0}, glm::vec3(.4f)); shared.camera = cameraAt({0, 1, 0});
    require(shared.input.press(Hand::Left, shared.camera, shared.boxes, shared.world, 0) &&
            shared.input.press(Hand::Right, shared.camera, shared.boxes, shared.world, 0), "Both hands must be able to share a box");
    shared.advance(90);
    require(shared.boxes.pose(0).center.y < 1.1f, "Two stationary hands must not double gravity compensation and launch a box");
    shared.input.moveMouse(100, 100, 2);
    const auto velocity = shared.boxes.velocity(0);
    shared.input.release(Hand::Left, 2.01);
    require(shared.input.heldBox(Hand::Right) == 0 && glm::distance(velocity, shared.boxes.velocity(0)) < .001f,
            "Releasing one shared grip must not throw an object still held by the other hand");
}
float lift(float size, bool twoHands, float dt = 1.0f / 60) {
    Fixture f;
    const glm::vec3 center(0, size * .5f + .02f, 0);
    f.boxes.add(center, glm::vec3(size)); f.camera = cameraAt(center, size * .5f + 1);
    require(f.input.press(Hand::Left, f.camera, f.boxes, f.world, 0), "Lift fixture must be grabbable");
    if (twoHands) require(f.input.press(Hand::Right, f.camera, f.boxes, f.world, 0), "Second lifting hand must attach");
    f.input.moveMouse(0, 250, .01);
    f.advance(static_cast<int>(3 / dt), dt);
    return bottom(f.boxes.pose(0));
}
void heldMovementAndDrop() {
    Fixture f;
    f.boxes.add({0, .22f, 0}, glm::vec3(.4f)); f.camera = cameraAt({0, .22f, 0});
    const auto hit = f.boxes.raycast(f.camera.position(), f.camera.direction(), 2);
    require(hit && f.input.press(Hand::Left, f.camera, f.boxes, f.world, 0), "Movement fixture must be grabbable");
    f.input.moveMouse(200, 200, .01); f.advance(120);
    auto point = f.boxes.pointState(0, hit->localPoint).point;
    require(point.x > .6f && point.y > .8f, "Mouse right/up must move the attached point sideways and upward");
    const auto raised = point;
    f.input.moveMouse(-400, -200, 2.01); f.advance(120);
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
    f.input.moveMouse(0, 200, 8.01); f.advance(120);
    require(bottom(f.boxes.pose(0)) > .5f, "Drop fixture must be airborne while held");
    const auto velocity = f.boxes.velocity(0);
    f.input.release(Hand::Left, 10.01);
    require(glm::distance(velocity, f.boxes.velocity(0)) < .001f, "A release without a recent swing must preserve existing momentum without a boost");
    f.advance(120);
    require(bottom(f.boxes.pose(0)) < .04f, "Released objects must fall and settle onto the floor");
}
void liftingDraggingAndCollisions() {
    const float light = lift(.4f, false), heavy = lift(1, false);
    const float mediumOne = lift(.73f, false), mediumTwo = lift(.73f, true);
    std::cout << "Lift bottoms: light=" << light << " heavy=" << heavy << " medium one=" << mediumOne << " two=" << mediumTwo << '\n';
    require(light > .5f, "A small crate must lift well clear of the ground");
    require(heavy < .06f && mediumOne < .06f, "Objects above one-hand lifting strength must remain supported by the floor");
    require(mediumTwo > .25f, "Combined hands must lift an object too heavy for one hand");
    const auto slow = lift(.4f, false, 1.0f / 30), fast = lift(.4f, false, 1.0f / 120);
    require(std::abs(slow - fast) < .15f, "Holding must work across frames with multiple or zero physics steps");

    Fixture drag;
    drag.boxes.add({0, .52f, 0}, glm::vec3(1)); drag.camera = cameraAt({0, .52f, 0}, 1.5f);
    require(drag.input.press(Hand::Right, drag.camera, drag.boxes, drag.world, 0), "Heavy box must be grabbable for dragging");
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
    require(blocked.input.press(Hand::Left, blocked.camera, blocked.boxes, blocked.world, 0), "Box before a wall must be reachable");
    blocked.input.moveMouse(375, 0, .01); blocked.advance();
    require(blocked.boxes.pose(0).center.x < .65f, "Holding must not teleport or pull a box through a solid wall");
    Fixture thrown(true);
    thrown.boxes.add({0, 1, 0}, glm::vec3(.4f)); thrown.camera = cameraAt({0, 1, 0});
    require(thrown.input.press(Hand::Left, thrown.camera, thrown.boxes, thrown.world, 0), "Throw collision fixture must be reachable");
    thrown.input.moveMouse(1500, 0, .01); thrown.input.release(Hand::Left, .02); thrown.advance();
    require(thrown.boxes.pose(0).center.x < .65f, "A fast release throw must collide with a thin static wall");
}
void routingAndCancellation() {
    for (const bool boxInFront : {false, true}) {
        Fixture f;
        viewer::DoorPanel panel;
        panel.hinge = {-.5f, 1, 0}; panel.center = {0, 1, 0}; panel.halfExtents = {.5f, 1, .02f}; panel.outward = {0, 0, 1};
        viewer::DoorLayer doors({panel}, f.physics);
        viewer::CollisionWorld world(f.model, {}, &doors); world.setBoxes(&f.boxes);
        f.boxes.add({0, 1, boxInFront ? .65f : -.5f}, glm::vec3(.4f));
        viewer::PlayerInteraction input; input.toggleMode(); f.camera = cameraAt({0, 1, 0}, 1.3f);
        require(input.press(Hand::Left, f.camera, doors, f.boxes, world, 0), "Nearest reachable target must be grabbed");
        require(boxInFront ? input.heldBox(Hand::Left).has_value() : input.grabbedDoor().has_value(), "Left hand must select the nearer door or prop");
        input.look(f.camera, 100, 100, .01);
        const auto before = f.boxes.velocity(0);
        input.toggleMode();
        require(input.mode() == viewer::ControlMode::Combat && !input.grabbing() && glm::distance(before, f.boxes.velocity(0)) < .001f,
                "Mode changes must cancel door and object grips without throwing");
    }
}
} // namespace
int main() {
    try {
        picking(); throws(); independentHandsAndLook(); heldMovementAndDrop(); liftingDraggingAndCollisions(); routingAndCancellation();
        std::cout << "Object picking, hands, throw directions, strength, movement and collision tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
