#include "door_interaction.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::DoorPanel panel(glm::vec3 normal = {0, 0, 1}, glm::vec3 offset = {}) {
    viewer::DoorPanel p;
    const auto right = glm::cross(glm::vec3(0, 1, 0), normal);
    p.hinge = offset + glm::vec3(0, 1, 0);
    p.center = p.hinge + right * .5f;
    p.halfExtents = {.5f, 1, .02f};
    p.outward = normal;
    p.rotation = glm::quat_cast(glm::mat3(right, glm::vec3(0, 1, 0), normal));
    p.closedTransform = glm::translate(glm::mat4(1), p.center) * glm::mat4_cast(p.rotation);
    return p;
}
viewer::Camera cameraAt(glm::vec3 point, float distance = 1) {
    viewer::Camera camera;
    camera.look(0, 200); // Level the default -20 degree pitch.
    camera.setPosition(point - camera.direction() * distance);
    return camera;
}
viewer::Model doorModel() {
    viewer::Model model;
    viewer::Primitive mesh;
    for (const auto point : {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 2, 0), glm::vec3(0, 2, 0)}) {
        viewer::Vertex v; v.position = point; mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(mesh);
    model.draws.push_back({0, glm::mat4(1), "building_1_doors", 120, false, "building_1"});
    return model;
}
struct Fixture {
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors{{panel()}, physics};
    viewer::CollisionWorld world{doorModel(), {}, &doors};
    viewer::Camera camera = cameraAt({.8f, 1, .02f});
    viewer::DoorInteraction input;
    void grab() {
        input.toggleMode();
        require(input.press(camera, doors, world), "A visible door within reach must be grabbable");
    }
    void advance(int frames = 120, float dt = 1.0f / 120) {
        for (int i = 0; i < frames; ++i) {
            input.update(camera, dt);
            physics->step(dt);
        }
    }
};

void rayQueries() {
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors({panel({0, 0, 1}, {0, 0, -1}), panel()}, physics);
    const auto hit = doors.raycast({.8f, 1, 1.52f}, {0, 0, -4}, 1.5f);
    require(hit && hit->door == 1 && std::abs(hit->distance - 1.5f) < 1e-5f,
            "Ray must normalize direction, accept the reach boundary and choose the nearest panel");
    require(doors.activeCount() == 0, "Picking must not instantiate bodies");
    require(!doors.raycast({.8f, 1, 1.521f}, {0, 0, -1}, 1.5f), "Door beyond reach must miss");
    require(!doors.raycast({1.1f, 1, 1}, {0, 0, -1}, 1.5f), "Ray beside the door must miss");
    require(!doors.raycast({.8f, 2.1f, 1}, {0, 0, -1}, 1.5f), "Ray above the door must miss");
    require(!doors.raycast({}, {}, 1.5f), "Zero direction must be rejected");
    require(!doors.raycast({}, {0, 0, -1}, std::numeric_limits<float>::quiet_NaN()), "Nonfinite reach must be rejected");

    for (const auto normal : {glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(1, 0, 2))}) {
        const auto p = panel(normal, {14, 3, -8});
        viewer::DoorLayer rotated({p}, physics);
        const auto edge = p.center + glm::cross(glm::vec3(0, 1, 0), normal) * .3f;
        const auto first = rotated.raycast(edge + normal, -normal, 1.5f);
        require(first.has_value(), "Rotated, translated panel must be pickable");
        rotated.applyImpulse(0, normal * 10.0f, first->point);
        for (int i = 0; i < 20; ++i) physics->step(1.0f / 60);
        const auto attached = rotated.pointState(0, first->localPoint);
        const auto expected = glm::vec3(rotated.transform(0) * glm::inverse(p.closedTransform) * glm::vec4(first->point, 1));
        require(glm::distance(attached.point, expected) < .001f, "Selected point must remain attached to the moving panel");
        require(glm::distance(attached.point, first->point) > .1f, "Selected point must move with the door");
        const auto boxes = rotated.colliders(p.hinge - glm::vec3(3), p.hinge + glm::vec3(3));
        const auto liveNormal = boxes[0].rotation * glm::vec3(0, 0, 1);
        const auto liveHit = rotated.raycast(attached.point + liveNormal, -liveNormal, 1.5f);
        require(liveHit && glm::distance(liveHit->point, attached.point) < .002f,
                "Picking must use the open pose rather than the closed panel");
    }
}

void modesAndLook() {
    Fixture f;
    require(f.input.mode() == viewer::ControlMode::Combat, "Combat must be the startup mode");
    require(!f.input.press(f.camera, f.doors, f.world), "Combat click must not grab a door");
    auto yaw = f.camera.yawDegrees();
    f.input.look(f.camera, 10, 0);
    require(f.camera.yawDegrees() != yaw, "Combat mouse look must work");
    f.camera = cameraAt({.8f, 1, .02f});
    f.grab();
    yaw = f.camera.yawDegrees();
    const auto pitch = f.camera.pitchDegrees();
    f.input.look(f.camera, 50, -80);
    require(f.camera.yawDegrees() == yaw && f.camera.pitchDegrees() == pitch, "Dragging must freeze camera orientation");
    f.input.release();
    f.input.look(f.camera, 10, 5);
    require(f.camera.yawDegrees() != yaw && f.camera.pitchDegrees() != pitch, "Release must immediately restore mouse look");
    f.camera = cameraAt({.8f, 1, .02f});
    require(f.input.press(f.camera, f.doors, f.world), "Regrabbing must work");
    f.input.toggleMode();
    require(!f.input.grabbing() && f.input.mode() == viewer::ControlMode::Combat, "Mode switch must cancel a held grab");
    f.input.toggleMode();
    f.camera.setPosition({10, 1, 1});
    require(!f.input.press(f.camera, f.doors, f.world), "An interaction click with no target must not grab");
    yaw = f.camera.yawDegrees();
    f.input.look(f.camera, 10, 0);
    require(f.camera.yawDegrees() != yaw, "Mouse look must work after a missed grab");
}

void occlusion() {
    Fixture f;
    viewer::Model wall = doorModel();
    viewer::Primitive mesh;
    for (const auto point : {glm::vec3(0, 0, .5f), glm::vec3(1, 0, .5f), glm::vec3(1, 2, .5f), glm::vec3(0, 2, .5f)}) {
        viewer::Vertex v; v.position = point; mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    wall.primitives.push_back(mesh);
    wall.draws.push_back({1, glm::mat4(1), "building_1_walls"});
    viewer::CollisionWorld blocked(wall, {}, &f.doors);
    f.input.toggleMode();
    require(!f.input.press(f.camera, f.doors, blocked), "A wall must prevent grabbing through it");
    viewer::CollisionWorld trunk(doorModel(), {{{.8f, 0, .5f}, .15f, 2}}, &f.doors);
    require(!f.input.press(f.camera, f.doors, trunk), "A solid trunk must prevent grabbing through it");
}

void mouseAndMovementForces() {
    Fixture depth;
    depth.grab();
    depth.input.look(depth.camera, 0, -150);
    depth.advance();
    require(depth.doors.angle(0) > .3f, "Dragging down must pull an outside-facing door outward");
    depth.input.look(depth.camera, 0, 150);
    depth.advance(240);
    require(std::abs(depth.doors.angle(0)) < .08f, "Reverse vertical drag must return the door toward closed");

    Fixture horizontal;
    horizontal.doors.applyImpulse(0, {0, 0, 8}, {.8f, 1, 0});
    horizontal.advance(30);
    const auto point = horizontal.doors.pointState(0, {.3f, 0, .02f}).point;
    horizontal.camera = cameraAt(point);
    horizontal.grab();
    const auto angle = horizontal.doors.angle(0);
    horizontal.input.look(horizontal.camera, -150, 0);
    horizontal.advance();
    require(horizontal.doors.angle(0) > angle + .1f, "Horizontal drag must exert torque on an open door");
    horizontal.input.release();
    horizontal.camera = cameraAt(horizontal.doors.pointState(0, {.3f, 0, .02f}).point);
    require(horizontal.input.press(horizontal.camera, horizontal.doors, horizontal.world), "An open door must be grabbable again");
    const auto beforeStrafe = horizontal.doors.angle(0);
    horizontal.camera.move(0, 1, 0, .05f, false); // D: move the grip to the right.
    horizontal.advance();
    require(horizontal.doors.angle(0) < beforeStrafe - .1f, "Strafing while holding must tug the open door sideways");

    Fixture movement;
    movement.grab();
    movement.camera.move(-1, 0, 0, .05f, false); // S: walk backwards from the door.
    movement.advance();
    require(movement.doors.angle(0) > .2f, "Moving away while holding must pull the selected point");
    movement.camera.move(1, 0, 0, .05f, false); // W: return to the initial eye position.
    movement.advance(240);
    require(std::abs(movement.doors.angle(0)) < .08f, "Moving back must push the held point toward closed");

    Fixture height;
    height.grab();
    height.camera.setPosition(height.camera.position() + glm::vec3(0, 1, 0));
    height.advance();
    require(std::abs(height.doors.angle(0)) < .001f, "Vertical player displacement must not generate door torque");
}

float dragAtRate(float dt) {
    Fixture f;
    f.grab();
    f.input.look(f.camera, 0, -150);
    f.advance(static_cast<int>(std::round(1.0f / dt)), dt);
    return f.doors.angle(0);
}
void frameRates() {
    const float slow = dragAtRate(1.0f / 30), normal = dragAtRate(1.0f / 60), fast = dragAtRate(1.0f / 240);
    require(slow > .3f && fast > .3f && std::abs(slow - normal) < .08f && std::abs(fast - normal) < .08f,
            "Grab forces must behave consistently across frames with zero or multiple physics steps");
}
void applicationPoint() {
    Fixture hinge, edge;
    hinge.camera = cameraAt({.001f, 1, .02f});
    hinge.grab();
    edge.grab();
    hinge.input.look(hinge.camera, 0, -150);
    edge.input.look(edge.camera, 0, -150);
    hinge.advance();
    edge.advance();
    // Both surface hits have finite leverage; compare their resulting motion
    // rather than treating the collider's front face as the exact hinge axis.
    require(std::abs(hinge.doors.angle(0)) < edge.doors.angle(0) * .25f && edge.doors.angle(0) > .3f,
            "Force must act at the picked point: near the hinge has little leverage, the free edge has much more");
}
} // namespace
int main() {
    try {
        rayQueries(); modesAndLook(); occlusion(); mouseAndMovementForces(); frameRates(); applicationPoint();
        std::cout << "Door interaction tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Door interaction test failed: " << e.what() << '\n';
        return 1;
    }
}
