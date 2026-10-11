#include "collision_world.hpp"
#include "door_layer.hpp"
#include "fps_controller.hpp"
#include "physics_world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void quad(viewer::Model& model, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const std::string& name, const glm::mat4& node, bool detail = false, bool low = false) {
    viewer::Primitive primitive;
    const auto normal = glm::normalize(glm::cross(b - a, c - a));
    for (const auto p : {a, b, c, d}) {
        viewer::Vertex v;
        v.position = glm::vec3(glm::inverse(node) * glm::vec4(p, 1));
        v.normal = glm::transpose(glm::mat3(node)) * normal;
        primitive.vertices.push_back(v);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), node, name, detail ? 120.0f : 0, low,
                          detail ? "building_test" : ""});
    model.primitives.push_back(std::move(primitive));
}
void wall(viewer::Model& model, float left, float bottom, float right, float top,
          const std::string& name, const glm::mat4& node, bool detail, bool low = false) {
    quad(model, {left, bottom, 0}, {right, bottom, 0}, {right, top, 0}, {left, top, 0}, name, node, detail, low);
}
viewer::Model fixture(const glm::mat4& node = glm::mat4(1)) {
    viewer::Model model;
    quad(model, {-20, 0, -20}, {-20, 0, 20}, {20, 0, 20}, {20, 0, -20}, "terrain", node);
    wall(model, -3, -.08f, 3, 3, "building_test_walls", node, true, true);
    wall(model, -3, -.08f, -.5f, 3, "building_test_detailed_walls", node, true);
    wall(model, .5f, -.08f, 3, 3, "building_test_detailed_walls", node, true);
    wall(model, -.5f, 2.1f, .5f, 3, "building_test_detailed_walls", node, true);
    wall(model, -.5f, -.08f, .5f, 2.1f, "building_test_doors", node, true);
    // Glass never creates a walkable opening in the coarse collision shell.
    wall(model, 1.5f, .9f, 2.5f, 2.1f, "building_test_windows", node, true);
    return model;
}
void walk(viewer::FPSController& player, const viewer::CollisionWorld& world, float yaw = -90, bool run = false) {
    for (int i = 0; i < 120; ++i) player.update(1.0f / 120, {1, 0, yaw, run, false}, world);
}
void open(viewer::DoorLayer& doors, viewer::PhysicsWorld& physics) {
    const auto& p = doors.panel(0);
    const auto free = p.center + glm::cross(glm::vec3(0, 1, 0), p.outward) * (p.halfExtents.x * .9f);
    for (int i = 0; i < 180; ++i) {
        const auto point = glm::vec3(doors.transform(0) * glm::inverse(p.closedTransform) * glm::vec4(free, 1));
        doors.applyForce(0, glm::cross(glm::vec3(0, -1, 0), point - p.hinge) * 100.0f, point);
        physics.step(1.0f / 60);
    }
}

void closedAndOpenPassage(const glm::mat4& node, bool run) {
    auto model = fixture(node);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    require(!world.contacts({0, .002f, .15f}, .3f, 1.8f).empty(), "An inactive closed door must physically block the capsule");
    require(doors.activeCount() == 0, "Collision queries must not activate or mutate door physics");
    require(!world.lineOfSight({0, 1, 1}, {0, 1, -1}), "A closed physical door must block sight");
    viewer::FPSController player;
    require(player.spawn({0, 1.7f, 1.5f}, world), "Player must spawn in front of the door");
    walk(player, world, -90, run);
    require(player.feet().z >= .319f, "Walking/running must stop against the 4 cm closed door collider");
    open(doors, *physics);
    require(world.lineOfSight({0, 1, 1}, {0, 1, -1}), "An uncovered doorway must allow sight in both directions");
    require(world.lineOfSight({0, 1, -1}, {0, 1, 1}), "Doorway sight must also work from inside");
    walk(player, world, -90, run);
    require(player.feet().z < -1, "Walking/running must cross the opening once the panel is physically clear");
    require(std::abs(player.feet().y) < .01f && player.grounded(), "Door traversal must keep terrain support");
    player.teleport({0, .002f, -1.5f});
    walk(player, world, 90, run);
    require(player.feet().z > 1, "The same open doorway must be traversable from inside to outside");
    require(!world.contacts({.4f, .002f, 0}, .3f, 1.8f).empty(), "A capsule overlapping the jamb must still hit masonry");
    require(!world.contacts({0, .002f, 0}, .3f, 2.3f).empty(), "The doorway lintel must block an over-height capsule");
    require(!world.contacts({2, .002f, .15f}, .3f, 1.8f).empty(), "Window locations must retain solid walking collision");
    const auto collider = doors.colliders({-2, 0, -2}, {2, 3, 2}).at(0);
    require(!world.contacts({collider.center.x, .002f, collider.center.z}, .3f, 1.8f).empty(),
            "An open panel must still block a capsule that tries to occupy its physical location");
    require(!world.lineOfSight(collider.center + glm::vec3(0, 0, .4f), collider.center - glm::vec3(0, 0, .4f)),
            "Sight through the swung panel must remain blocked");
    require(!world.debugLines(collider.center, 2).empty(), "Collision overlay must include the moving door");
}

void partialClearanceUsesCapsuleGeometry() {
    auto model = fixture();
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    const auto& p = doors.panel(0);
    doors.applyImpulse(0, p.outward * 20.0f, p.center + glm::vec3(.45f, 0, 0));
    int steps = 0;
    while (doors.angle(0) < glm::radians(35.0f) && steps++ < 120) physics->step(1.0f / 60);
    require(doors.angle(0) > glm::radians(35.0f) && doors.angle(0) < glm::radians(40.0f), "Fixture must leave a narrow gap");
    viewer::FPSController player;
    require(player.spawn({0, 1.7f, 1.5f}, world), "Standard player must spawn before the partial door");
    walk(player, world);
    require(player.feet().z > 0, "A partially open panel must block a capsule too wide for the remaining gap");
    viewer::FPSConfig small;
    small.radius = .04f;
    viewer::FPSController narrow(small);
    require(narrow.spawn({.44f, 1.7f, 1.5f}, world), "Narrow test capsule must spawn before the same gap");
    walk(narrow, world);
    require(narrow.feet().z < -1, "The same door pose must pass a capsule that physically fits the remaining gap");
}

void legacyOverlayKeepsVisibleWall() {
    auto model = fixture();
    for (auto& draw : model.draws) { draw.lowDetail = false; draw.detailGroup.clear(); }
    model.draws[1].detailDistance = 0; // Uncut legacy shell is always visible.
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    open(doors, *physics);
    require(!world.contacts({0, .002f, .15f}, .3f, 1.8f).empty(), "Legacy overlay maps must not gain invisible holes through their visible walls");
}

void generatedMapPassage(const std::filesystem::path& path) {
    auto model = viewer::loadModel(path);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    require(doors.count() > 0, "Generated fixture must contain hinged door panels");
    // Select a clear, nearly flat approach using actual rendered terrain and
    // static collision. Do not assume the configured map's origin or elevations.
    const auto feetAt = [&](glm::vec3 position) -> std::optional<glm::vec3> {
        const auto hit = world.groundAt(position.x, position.z, model.boundsMin.y - 1, model.boundsMax.y + 1, true);
        if (!hit || hit->normal.y < .99f) return std::nullopt;
        position.y = hit->height + .3f * (1 / hit->normal.y - 1) + .01f;
        return position;
    };
    for (std::size_t i = 0; i < std::min<std::size_t>(doors.count(), 512); ++i) {
        const auto& p = doors.panel(i);
        const auto approach = feetAt(p.center + p.outward * .9f);
        const auto threshold = feetAt(p.center);
        if (!approach || !threshold || !world.contacts(*approach, .3f, 1.8f).empty() ||
            world.contacts(*threshold, .3f, 1.8f).empty() || world.insideBuilding(*approach, .3f, 1.8f)) continue;
        const auto freeEdge = p.center + glm::cross(glm::vec3(0, 1, 0), p.outward) * (p.halfExtents.x * .9f);
        for (int step = 0; step < 180; ++step) {
            const auto point = glm::vec3(doors.transform(i) * glm::inverse(p.closedTransform) * glm::vec4(freeEdge, 1));
            doors.applyForce(i, glm::cross(glm::vec3(0, -1, 0), point - p.hinge) * 100.0f, point);
            physics->step(1.0f / 60);
        }
        bool corridorClear = true;
        for (const float distance : {-.7f, -.4f, 0.0f, .4f, .7f}) {
            const auto feet = feetAt(p.center + p.outward * distance);
            if (!feet || !world.contacts(*feet, .3f, 1.8f).empty()) corridorClear = false;
        }
        if (!corridorClear) continue;
        viewer::FPSController player;
        player.teleport(*approach);
        walk(player, world, glm::degrees(std::atan2(-p.outward.z, -p.outward.x)));
        require(glm::dot(player.feet() - p.center, p.outward) < -.5f,
                "A player must traverse an actual generated doorway after its physical panel moves clear");
        require(player.grounded(), "Actual-map doorway traversal must preserve ground support");
        std::cout << "Traversed generated door " << i << " of " << doors.count() << "; collision triangles: "
                  << world.stats().triangles << '\n';
        return;
    }
    throw std::runtime_error("Could not find a clear generated doorway for the walking regression");
}
} // namespace

int main(int argc, char** argv) {
    try {
        closedAndOpenPassage(glm::mat4(1), false);
        closedAndOpenPassage(glm::rotate(glm::mat4(1), -glm::half_pi<float>(), glm::vec3(1, 0, 0)), true);
        partialClearanceUsesCapsuleGeometry(); legacyOverlayKeepsVisibleWall();
        if (argc == 2) generatedMapPassage(argv[1]);
        std::cout << "Door walking tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Door walking test failed: " << error.what() << '\n';
        return 1;
    }
}
