#include "interior_fixture.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "fps_controller.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Fixture {
    viewer::Model model = interior_fixture::town();
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes{physics};
    viewer::CollisionWorld world{model};
    viewer::PhysicsScene scene{physics, world, model.boundsMin, model.boundsMax};
    Fixture() { world.setBoxes(&boxes); scene.ensureNear({-10, -1, -10}, {10, 5, 10}); }
    void tick(viewer::FPSController& player, viewer::FPSInput input = {}, float dt = 1.0f / 60) {
        scene.prepare(boxes); physics->step(dt); player.update(dt, input, world);
    }
};
void supportAndJump() {
    Fixture f;
    const auto index = f.boxes.add({0, .245f, 0}, {.8f, .45f, .8f});
    viewer::FPSController player;
    require(player.startFalling({0, 1.7f, 1.3f}, f.world), "Player must enter walking beside the crate");
    for (int i = 0; i < 60; ++i) f.tick(player);
    for (int i = 0; i < 20; ++i) f.tick(player, {1, 0, -90, false, i == 0});
    for (int i = 0; i < 120; ++i) f.tick(player);
    require(player.grounded() && player.feet().y > .40f && player.feet().y < .50f, "Jumping onto a crate must land on its live top face");
    require(glm::distance(glm::vec2(f.boxes.pose(index).center.x, f.boxes.pose(index).center.z), glm::vec2(0)) < .03f,
            "Standing or jumping on top must not push the box sideways");
    const auto ground = f.world.groundAt(0, 0, 0, 2);
    require(ground && ground->height > .4f, "Ordinary support query must select the crate above terrain");
    require(std::abs(f.world.groundAt(0, 0, -1, 2, true)->height) < .001f, "Terrain-only placement must ignore props");
    require(std::abs(f.world.staticGroundAt(0,0,-1,2)->height)<.001f, "Cached blood floors must ignore movable crates");
    const auto queryRevision = f.boxes.revision();
    for (int i = 0; i < 60; ++i) {
        f.world.contacts({.5f, 0, 0}, .3f, 1.8f);
        f.world.lineOfSight({0, .2f, 2}, {0, .2f, -2});
    }
    require(f.boxes.revision() == queryRevision && !f.world.lineOfSight({0, .2f, 2}, {0, .2f, -2}), "Box must occlude sight with read-only queries");
    const auto sleepRevision = f.boxes.revision();
    for (int i = 0; i < 30; ++i) f.tick(player);
    require(f.boxes.revision() == sleepRevision, "Idle player support must let boxes sleep without render revisions");
}
float pushAtRate(float dt) {
    Fixture f;
    f.boxes.add({0, .52f, 0}, {1, 1, 1});
    viewer::FPSController player;
    require(player.startFalling({0, 1.7f, 2}, f.world), "Player must enter walking in front of a pushable box");
    for (int i = 0; i < static_cast<int>(2 / dt); ++i) f.tick(player, {1, 0, -90}, dt);
    const auto box = f.boxes.pose(0);
    require(box.center.z < -1 && player.feet().z < .5f, "Walking into a box must move it and permit player progress");
    require(box.center.y > .3f && player.feet().y < .05f, "Pushing must preserve terrain support");
    return box.center.z;
}
void wallAndStack() {
    Fixture f;
    f.boxes.add({0, .5f, -5.4f}, {1, 1, 1});
    f.boxes.add({3, .52f, 0}, {1, 1, 1});
    f.boxes.add({3, 1.55f, 0}, {.6f, 1, .6f});
    viewer::FPSController player;
    require(player.startFalling({0, 1.7f, -3}, f.world), "Player must enter walking before box and wall");
    for (int i = 0; i < 180; ++i) f.tick(player, {1, 0, -90});
    require(f.boxes.pose(0).center.z > -5.55f && player.feet().z > -5,
            "Pushed boxes must collide with the actual interior wall");
    require(f.boxes.pose(2).center.y > 1.3f && f.boxes.pose(2).center.y < 1.7f, "Dynamic boxes must stack on each other");
}
void fallbackFloorsAreIsolated() {
    Fixture f;
    auto definition = b3DefaultBodyDef();
    definition.position = {0, 1, 0};
    const auto floor = b3CreateBody(f.physics->id(), &definition);
    auto shape = b3DefaultShapeDef();
    shape.filter.categoryBits = viewer::ragdollFloorCategory;
    const auto hull = b3MakeBoxHull(3, .05f, 3);
    b3CreateHullShape(floor, &shape, &hull.base);
    f.boxes.add({0, 2, 0}, {.6f, .6f, .6f});
    for (int i = 0; i < 180; ++i) f.physics->step(1.0f / 60);
    require(f.boxes.pose(0).center.y < .35f && f.boxes.pose(0).center.y > .25f,
            "Props must fall through ragdoll fallback floors onto actual terrain");
    b3DestroyBody(floor);
}
void openDoorway() {
    auto model = interior_fixture::town();
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::BoxLayer boxes(physics);
    viewer::CollisionWorld world(model, {}, &doors);
    world.setBoxes(&boxes);
    viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
    scene.ensureNear({-10, -1, -10}, {10, 5, 10});
    const auto& panel = doors.panel(0);
    const auto edge = panel.center + glm::cross(glm::vec3(0, 1, 0), panel.outward) * (panel.halfExtents.x * .9f);
    for (int i = 0; i < 120; ++i) {
        const auto point = glm::vec3(doors.transform(0) * glm::inverse(panel.closedTransform) * glm::vec4(edge, 1));
        doors.applyForce(0, glm::cross(glm::vec3(0, -1, 0), point - panel.hinge) * 100.0f, point);
        physics->step(1.0f / 60);
    }
    std::cout << "Doorway angle: " << glm::degrees(doors.angle(0)) << '\n';
    require(doors.angle(0) > glm::radians(85.0f), "Doorway fixture must be physically open");
    boxes.add({0, .42f, 5}, {.5f, .8f, .5f});
    viewer::FPSController player;
    require(player.startFalling({0, 1.7f, 3.5f}, world), "Player must start behind indoor box");
    for (int i = 0; i < 150; ++i) {
        scene.prepare(boxes); physics->step(1.0f / 60);
        player.update(1.0f / 60, {1, 0, 90}, world);
    }
    require(boxes.pose(0).center.z > 6.5f && player.feet().z > 6.2f,
            "Box and player must pass through the actual open doorway in both collision systems");
}
} // namespace
int main() {
    try {
        supportAndJump();
        const auto slow = pushAtRate(1.0f / 30), fast = pushAtRate(1.0f / 120);
        std::cout << "Push endpoints: 30 FPS=" << slow << ", 120 FPS=" << fast << '\n';
        require(std::abs(slow - fast) < .5f, "Walking pushes must behave consistently across frame rates");
        wallAndStack();
        fallbackFloorsAreIsolated(); openDoorway();
        std::cout << "Physics box support, jump, push, sleep and wall tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
