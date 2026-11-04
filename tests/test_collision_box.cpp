#include "collision_box.hpp"
#include "collision_world.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "vehicle_layer.hpp"
#include "physics_world.hpp"
#include "zombie_doorway_fixture.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool close(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) < tolerance; }

void thinFacesAndSlopes() {
    const std::array<glm::vec3, 3> floor{{{-10, 0, -10}, {0, 0, 10}, {10, 0, -10}}};
    viewer::CollisionBox box{{0, .215f, 0}};
    auto contact = viewer::collisionBoxTriangleContact(box, floor);
    require(contact && contact->normal.y > .999f && close(contact->depth, .01f),
        "A box crossing a zero-thickness floor needs its full upward penetration correction");
    auto reverse = floor; std::swap(reverse[1], reverse[2]);
    const auto back = viewer::collisionBoxTriangleContact(box, reverse);
    require(back && glm::distance(back->normal, contact->normal) < 1e-5f && close(back->depth, contact->depth),
        "Thin surface collision must be independent of triangle winding");
    box.center.y = -.215f;
    contact = viewer::collisionBoxTriangleContact(box, floor);
    require(contact && contact->normal.y < -.999f && close(contact->depth, .01f),
        "A thin ceiling must push a box on its lower side downward");
    box.center.y = .235f;
    require(!viewer::collisionBoxTriangleContact(box, floor), "A clear box must stay clear of the floor");
    contact = viewer::collisionBoxTriangleContact(box, floor, .02f);
    require(contact && close(contact->depth, .01f), "Contact skin must include a nearby thin face");

    const auto rotation = glm::angleAxis(.3f, glm::vec3(0, 0, 1));
    std::array<glm::vec3, 3> slope;
    for (std::size_t i = 0; i < slope.size(); ++i) slope[i] = rotation * floor[i];
    box.center = rotation * glm::vec3(0, .215f, 0); box.rotation = rotation;
    contact = viewer::collisionBoxTriangleContact(box, slope);
    require(contact && glm::dot(contact->normal, rotation * glm::vec3(0, 1, 0)) > .999f &&
        close(contact->depth, .01f), "Shallow slope contacts must retain the face normal and metric depth");

    box = {{0, 0, 0}, {.5f, .5f, .5f}};
    const std::array<glm::vec3, 3> separated{{{.4f, .7f, 0}, {.7f, .4f, 0}, {.7f, .7f, 0}}};
    require(!viewer::collisionBoxTriangleContact(box, separated),
        "Overlapping triangle/box AABBs must not create an edge contact across a separating axis");
    const std::array<glm::vec3, 3> edge{{{-.7f, .4f, 0}, {.7f, .4f, 0}, {0, .8f, 0}}};
    require(viewer::collisionBoxTriangleContact(box, edge).has_value(),
        "A triangle clipping a box corner must collide even when its vertices are outside the box");
}

void worldSurfacesAndTrunks() {
    viewer::Model model;
    viewer::test::quad(model, "terrain", {-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10});
    viewer::CollisionWorld world(model, {{{2, 0, 0}, .15f, 4}});
    viewer::CollisionBox box{{0, .215f, 0}};
    auto contacts = world.contacts(box);
    require(!contacts.empty() && contacts.front().terrain && contacts.front().terrainSupport &&
        contacts.front().normal.y > .999f && close(contacts.front().depth, .01f),
        "Box queries must include static terrain with support metadata");
    box.center = {2.4f, .225f, 0};
    contacts = world.contacts(box);
    const auto trunk = std::find_if(contacts.begin(), contacts.end(), [](const auto& c) { return !c.terrain; });
    require(trunk != contacts.end() && trunk->normal.x > .999f && close(trunk->depth, .05f),
        "A low box must collide with a BVH-selected trunk capsule without being lifted");

    viewer::Model runtime;
    viewer::test::quad(runtime, "interior_partition", {-3, 0, 0}, {3, 0, 0}, {3, 2, 0}, {-3, 2, 0});
    world.setRuntimeGeometry(runtime);
    box.center = {0, .225f, .45f};
    contacts = world.contacts(box);
    require(!contacts.empty() && contacts.front().normal.z > .999f && close(contacts.front().depth, .05f),
        "Streamed thin walls must block the complete box length");
    box.center.z = -.45f;
    contacts = world.contacts(box);
    require(!contacts.empty() && contacts.front().normal.z < -.999f,
        "Streamed thin walls must block boxes from both sides");
    box.center = {50, .225f, 50};
    require(world.contacts(box).empty(), "Distant BVH geometry must not leak into box queries");
}

void liveDoorsAndProps() {
    auto model = viewer::test::doorway();
    const auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::CollisionBox box{{0, .23f, .45f}};
    auto contacts = world.contacts(box);
    require(std::any_of(contacts.begin(), contacts.end(), [](const auto& c) { return c.door && c.normal.z > .99f; }),
        "A box must contact the current closed door and preserve the door marker");
    require(doors.activeCount() == 0, "Box contact queries must not activate dormant doors");
    doors.setAngle(0, glm::half_pi<float>());
    contacts = world.contacts(box);
    require(std::none_of(contacts.begin(), contacts.end(), [](const auto& c) { return c.door; }),
        "Box queries must follow an opened door's live pose");

    viewer::Model empty;
    viewer::CollisionWorld props(empty);
    viewer::BoxLayer boxes(physics);
    boxes.add({0, .225f, 0}, {.6f, .45f, 1}); props.setBoxes(&boxes);
    box.center = {.55f, .225f, 0};
    const auto revision = boxes.revision();
    contacts = props.contacts(box);
    require(contacts.size() == 1 && contacts.front().normal.x > .999f && close(contacts.front().depth, .05f),
        "A box must collide with spatially selected live oriented props");
    require(boxes.revision() == revision && glm::length(boxes.velocity(0)) < 1e-6f,
        "Reading box contacts must not mutate or push physical props");
    props.pushBoxes(box, {-1, 0, 0}, 1.f / 60);
    require(boxes.velocity(0).x < -.01f, "Explicit box pushing must transfer a horizontal impulse into the prop");

    viewer::VehicleLayer cars(physics);
    cars.add({10, .8f, 0}); props.setVehicles(&cars);
    const auto chassis = cars.pose(0).chassis;
    box.center = chassis.center + chassis.rotation * glm::vec3(chassis.halfExtents.x + .25f, 0, 0);
    box.rotation = chassis.rotation;
    require(!props.contacts(box).empty(), "A box must collide with indexed live vehicle bodies");
}
} // namespace

int main() {
    try {
        thinFacesAndSlopes();
        worldSurfacesAndTrunks();
        liveDoorsAndProps();
        std::cout << "Oriented collision box checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
