#include "interior_fixture.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "door_interaction.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include "interior_objects.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void population(bool courtyard, const glm::mat4& node, std::uint32_t seed, std::array<int, 3>& sizes, std::set<std::size_t>& counts) {
    auto model = interior_fixture::town(courtyard, node);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::BoxLayer boxes(physics);
    viewer::CollisionWorld world(model, {}, &doors);
    world.setBoxes(&boxes);
    viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
    viewer::InteriorObjects interiors(model, doors, seed);
    viewer::DoorInteraction interaction;
    viewer::Camera camera;
    const auto& panel = doors.panel(0);
    camera.setPosition(panel.center + panel.outward);
    const auto direction = -panel.outward;
    const float yaw = glm::degrees(std::atan2(direction.z, direction.x));
    camera.look((yaw - camera.yawDegrees()) * 10, -camera.pitchDegrees() * 10);
    require(!interaction.press(camera, doors, world) && boxes.count() == 0, "Combat clicks must not populate interiors");
    interaction.toggleMode();
    require(interaction.press(camera, doors, world), "Fixture door must be reachable for a real grab");
    const auto count = interiors.populate(*interaction.grabbedDoor(), world, scene, boxes);
    require(count >= 12 && count <= 36, "Ordinary buildings must receive a bounded random population");
    counts.insert(count);
    require(interiors.populate(0, world, scene, boxes) == 0 && interiors.populate(1, world, scene, boxes) == 0 && boxes.count() == count,
            "Grabbing either door again must preserve the existing population");
    for (std::size_t i = 0; i < count; ++i) {
        const auto box = boxes.pose(i);
        const auto fullSize = box.halfExtents * 2.0f;
        ++sizes[fullSize.y < .65f ? 0 : fullSize.y < 1.3f ? 1 : 2];
        for (int corner = 0; corner < 4; ++corner) {
            const auto p = box.center + glm::vec3(corner & 1 ? box.halfExtents.x : -box.halfExtents.x, 0,
                                                  corner & 2 ? box.halfExtents.z : -box.halfExtents.z);
            const auto local = glm::vec3(glm::inverse(node) * glm::vec4(p, 1));
            require(local.x > -7 && local.x < 7 && local.z > -6 && local.z < 6, "Every box corner must be within the transformed footprint");
            if (courtyard) require(std::abs(local.x) > 1 || std::abs(local.z) > 1, "Boxes must avoid the courtyard hole");
        }
        require(box.center.y - box.halfExtents.y > model.boundsMin.y && box.center.y + box.halfExtents.y < model.boundsMax.y,
                "Boxes must have ground and roof clearance");
    }
    for (int frame = 0; frame < 180; ++frame) { scene.prepare(boxes); physics->step(1.0f / 60); }
    for (std::size_t i = 0; i < count; ++i) {
        const auto p = boxes.pose(i);
        require(std::abs(p.center.y - p.halfExtents.y - node[3].y) < .035f, "Spawned boxes must settle on actual terrain");
    }
}
void unsupportedInteriors() {
    for (const bool missingRoof : {false, true}) {
        auto model = interior_fixture::town();
        model.draws.erase(std::remove_if(model.draws.begin(), model.draws.end(), [&](const viewer::DrawInstance& draw) {
            return missingRoof ? draw.name.find("_roof") != std::string::npos : draw.name == "terrain";
        }), model.draws.end());
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        viewer::BoxLayer boxes(physics);
        viewer::CollisionWorld world(model, {}, &doors);
        viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
        viewer::InteriorObjects interiors(model, doors, 17);
        require(interiors.populate(0, world, scene, boxes) == 0 && boxes.count() == 0,
                "Missing roof or terrain must not create unsupported props");
    }
}
void realMap(const std::filesystem::path& path) {
    auto model = viewer::loadModel(path);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::BoxLayer boxes(physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
    viewer::InteriorObjects interiors(model, doors, 71);
    std::size_t populated = 0;
    for (std::size_t door = 0; door < std::min(doors.count(), std::size_t(128)) && populated < 5; ++door)
        if (interiors.populate(door, world, scene, boxes)) ++populated;
    require(populated == 5 && boxes.count() > 30, "Published map must support physical interior population");
    for (std::size_t i = 0; i < boxes.count(); ++i) {
        const auto box = boxes.pose(i);
        require(world.insideBuilding(box.center - glm::vec3(0, box.halfExtents.y, 0), .01f, box.halfExtents.y * 2),
                "Real-map boxes must be within actual building roofs");
    }
    for (int i = 0; i < 120; ++i) { scene.prepare(boxes); physics->step(1.0f / 60); }
    for (std::size_t i = 0; i < boxes.count(); ++i) {
        const auto box = boxes.pose(i);
        const auto ground = world.groundAt(box.center.x, box.center.z, model.boundsMin.y - 1, box.center.y, true);
        require(ground && box.center.y > ground->height && box.center.y < ground->height + 2.5f,
                "Real-map boxes must remain supported after physics settles");
    }
    std::cout << "Published map: " << boxes.count() << " boxes settled in " << populated << " buildings\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::array<int, 3> sizes{};
        std::set<std::size_t> counts;
        for (std::uint32_t seed = 0; seed < 24; ++seed) population(seed % 2, glm::mat4(1), seed, sizes, counts);
        const auto transformed = glm::translate(glm::mat4(1), {35, 5, -27}) *
            glm::rotate(glm::mat4(1), glm::radians(31.0f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1});
        population(true, transformed, 19, sizes, counts);
        const float fraction = static_cast<float>(sizes[0]) / (sizes[0] + sizes[1] + sizes[2]);
        require(fraction > .65f && fraction < .85f && sizes[1] > sizes[2] && sizes[2] > 10,
                "Population must favor small crates, retain medium boxes, and include rare cabinets");
        require(counts.size() > 8, "Box counts must vary between buildings/sessions");
        unsupportedInteriors();
        if (argc > 1) realMap(argv[1]);
        std::cout << "Interior population tests passed: " << sizes[0] << " crates, " << sizes[1] << " medium, " << sizes[2] << " cabinets\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
