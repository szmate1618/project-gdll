#include "house_fixture.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "door_interaction.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include "box_layer.hpp"
#include "model_merge.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
float area(const std::vector<glm::vec2>& polygon) {
    double sum = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        sum += double(a.x) * b.y - double(a.y) * b.x;
    }
    return static_cast<float>(std::abs(sum) * .5);
}
bool identical(const viewer::Model& a, const viewer::Model& b) {
    if (a.draws.size() != b.draws.size() || a.primitives.size() != b.primitives.size()) return false;
    for (std::size_t i = 0; i < a.primitives.size(); ++i) {
        const auto& x = a.primitives[i]; const auto& y = b.primitives[i];
        if (x.indices != y.indices || x.vertices.size() != y.vertices.size()) return false;
        for (std::size_t j = 0; j < x.vertices.size(); ++j)
            if (x.vertices[j].position != y.vertices[j].position || x.vertices[j].normal != y.vertices[j].normal) return false;
    }
    return true;
}
void streaming(const glm::mat4& node) {
    auto model = house_fixture::house(node);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::BoxLayer boxes(physics);
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    viewer::FloorplanLayer layer(model, doors, collision);
    const auto point = [&](glm::vec3 p) { return glm::vec3(node * glm::vec4(p, 1)); };
    require(doors.count() == 10 && !doors.present(2), "Runtime IDs must be reserved without panel geometry");
    layer.update({point({0, 1.7f, 8})});
    require(layer.residentCount() == 0, "Closed exterior doors must not generate interiors");
    viewer::ContactCandidates doorCache;
    std::size_t doorContacts = 0;
    const viewer::ContactVisitor countDoor{&doorContacts, [](void* p, const viewer::Contact&) {
        ++*static_cast<std::size_t*>(p); return true;
    }};
    collision.visitContacts(point({0, .1f, -4}), .25f, 1.7f, 0, 0, countDoor, &doorCache);
    require(doorContacts == 0, "Bare entry fixture must initially certify a door-free region");
    doors.setAngle(0, .7f);
    layer.update({point({0, 1.7f, -26.1f})});
    require(layer.residentCount() == 0, "Generation must not exceed the 20 metre door radius");
    layer.update({point({0, 1.7f, -25.9f})});
    require(layer.residentCount() == 1, "Either open entrance must generate the complete house layout");
    collision.visitContacts(point({0, .1f, -4}), .25f, 1.7f, 0, 0, countDoor, &doorCache);
    require(doorContacts > 0, "Generation must invalidate a previously cached door-free region");
    const auto& plan = layer.resident().begin()->second;
    float totalArea = 0; std::size_t livingRooms = 0;
    for (const auto& room : plan.rooms) { totalArea += area(room.polygon); livingRooms += room.kind == viewer::HouseRoom::Kind::Living; }
    require(std::abs(totalArea - 120) < .01f && livingRooms >= 1 && livingRooms <= 3,
            "Room polygons must tile the footprint with one to three living rooms");
    const auto entry = std::find_if(plan.doors.begin(), plan.doors.end(), [](const viewer::HouseInteriorDoor& d) { return d.entrance == 0; });
    require(entry != plan.doors.end(), "Streaming fixture requires its first entry partition");
    const auto entryId = entry->id;
    std::vector<std::size_t> generatedIds;
    for (const auto& door : plan.doors) generatedIds.push_back(door.id);
    require(plan.upperFloors.size() == 1 && std::abs(plan.upperFloors[0] - (node[3].y + 3)) < .001f,
            "Second window row must create the separating upper floor");
    const auto structure = plan.structure;
    const auto render = layer.renderModel();
    const auto revision = layer.revision();
    layer.update({point({0, 1.7f, 7})});
    require(layer.revision() == revision, "Unchanged proximity must not regenerate or upload geometry");
    doors.setAngle(0, 0);
    layer.update({point({0, 1.7f, 7})});
    require(layer.residentCount() == 1, "Closing the exterior door must retain the nearby interior");
    glm::vec3 upper(0);
    bool foundSupport = false;
    for (const float x : {-3.f, 0.f, 3.f}) for (const float z : {-2.f, 0.f, 2.f}) {
        const auto p = point({x, 3, z});
        const auto ground = collision.walkingGroundAt(p.x, p.z, p.y - .05f, p.y + .05f);
        if (ground && ground->normal.y > .99f && std::abs(ground->height - p.y) < .001f) { upper = p; foundSupport = true; }
    }
    require(foundSupport, "Upper storey must retain usable floor around its staircase hole");
    viewer::WalkingGroundCache groundCache;
    const auto support = collision.walkingGroundAt(upper.x, upper.z, upper.y - .2f, upper.y + .2f, &groundCache);
    require(support && std::abs(support->height - upper.y) < .001f && !support->terrain, "Walking must stand on the generated upper plane");
    const auto wallFront = point({2, 1, -5}), wallBack = point({2, 1, -3});
    require(!collision.lineOfSight(wallFront, wallBack), "Generated entry partition must block sight");
    require(collision.lineOfSight(point({0, 1, -5}), point({0, 1, -4.2f})), "Entry space must remain unobstructed before its interior door");
    const auto wallFeet = point({2, .1f, -4});
    viewer::ContactCandidates contacts;
    std::size_t count = 0;
    collision.visitContacts(wallFeet, .25f, 1.7f, 0, 0, {&count, [](void* p, const viewer::Contact&) { ++*static_cast<std::size_t*>(p); return true; }}, &contacts);
    require(count > 0, "Entry walls must participate in cached capsule collision");
    doors.setAngle(entryId, -.8f);
    require(glm::dot(glm::vec3(doors.transform(entryId) * glm::vec4(doors.panel(entryId).center, 1)) - doors.panel(entryId).center,
                     doors.panel(entryId).outward) < -.1f, "Negative interior angle must swing the panel into the living space");
    doors.setAngle(entryId, 0);
    const auto& panel = doors.panel(entryId);
    doors.applyImpulse(entryId, -panel.outward * 4.f, panel.center + glm::cross(glm::vec3(0, 1, 0), panel.outward) * .4f);
    for (int i = 0; i < 90; ++i) { scene.prepare(boxes); physics->step(1.f / 60); }
    require(doors.angle(entryId) < -.05f && doors.angle(entryId) > -2, "Interactive hinge impulses must open inward within the hinge limit");
    // Box support is uploaded even when the terrain tile was already prepared.
    const auto boxPosition = upper + glm::vec3(0, .8f, 0);
    boxes.add(boxPosition, {.5f, .5f, .5f});
    for (int i = 0; i < 120; ++i) { scene.prepare(boxes); physics->step(1.f / 60); }
    require(std::abs(boxes.pose(0).center.y - .25f - upper.y) < .05f, "Generated slabs must physically support props");
    layer.update({point({0, 1.7f, 37}), point({0, 1.7f, 0})});
    require(layer.residentCount() == 1, "A second nearby server observer must retain the interior");
    layer.update({point({0, 1.7f, 36})});
    require(layer.residentCount() == 1, "The 30 metre eviction boundary is inclusive from the building footprint");
    layer.update({point({0, 1.7f, 36.2f})});
    require(layer.residentCount() == 0 && layer.renderModel().draws.empty() &&
            std::all_of(generatedIds.begin(), generatedIds.end(), [&](std::size_t id) { return !doors.present(id); }),
            "Eviction must release all generated plans, meshes, and panels");
    require(!collision.walkingGroundAt(upper.x, upper.z, upper.y - .2f, upper.y + .2f, &groundCache),
            "Cached support must disappear when the slab is evicted");
    count = 0;
    collision.visitContacts(wallFeet, .25f, 1.7f, 0, 0, {&count, [](void* p, const viewer::Contact&) { ++*static_cast<std::size_t*>(p); return true; }}, &contacts);
    require(count == 0 && collision.lineOfSight(wallFront, wallBack), "Eviction must remove cached wall contacts and sight blockers");
    for (int i = 0; i < 150; ++i) { scene.prepare(boxes); physics->step(1.f / 60); }
    require(boxes.pose(0).center.y < upper.y - 1, "Eviction must remove Box3D support bodies too");
    doors.setAngle(1, .7f);
    layer.update({point({0, 1.7f, 8})});
    require(identical(structure, layer.resident().begin()->second.structure) && identical(render, layer.renderModel()),
            "Reentry through the other door must regenerate identical geometry and draw ordering");
    require(std::abs(doors.angle(entryId)) < .001f, "Evicted door movement must not be preserved locally");
}
void heuristic() {
    for (int variant = 0; variant < 6; ++variant) {
        auto model = house_fixture::house(variant == 0 ? glm::scale(glm::mat4(1), {.4f, 1, .4f}) :
                     variant == 1 ? glm::scale(glm::mat4(1), {2, 1, 2}) :
                     variant == 2 ? glm::scale(glm::mat4(1), {1.8f, 1, .45f}) : glm::mat4(1), false);
        if (variant == 3) interior_fixture::quad(model, {-1, 0, -1}, {1, 0, -1}, {1, 3, -1}, {-1, 3, -1}, "building_house_walls", glm::mat4(1), true);
        if (variant == 4) model.draws.erase(std::remove_if(model.draws.begin(), model.draws.end(), [](const viewer::DrawInstance& d) { return d.name == "building_house_roof"; }), model.draws.end());
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        viewer::CollisionWorld collision(model, {}, &doors);
        viewer::FloorplanLayer layer(model, doors, collision);
        require(doors.present(0), "Scaled house fixtures must retain a valid exterior door");
        doors.setAngle(0, .5f); layer.update({doors.panel(0).center});
        require(layer.residentCount() == (variant == 5 ? 1u : 0u), "House heuristic must reject sheds, large/elongated shells, courtyards, and missing roofs");
        if (variant == 5) require(layer.resident().begin()->second.upperFloors.empty(), "Single window row must not invent upper floors");
    }
}
void activationOrder() {
    auto model = house_fixture::house();
    auto second = house_fixture::house(glm::translate(glm::mat4(1), {60, 0, 0}));
    for (auto& draw : second.draws) {
        const auto pos = draw.name.find("building_house");
        if (pos == 0) draw.name.replace(0, 14, "building_other");
    }
    viewer::appendStaticModel(model, std::move(second));
    auto modelB = model;
    auto physicsA = std::make_shared<viewer::PhysicsWorld>(), physicsB = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer a(viewer::prepareDoorPanels(model), physicsA), b(viewer::prepareDoorPanels(modelB), physicsB);
    viewer::CollisionWorld collisionA(model, {}, &a), collisionB(modelB, {}, &b);
    viewer::FloorplanLayer plansA(model, a, collisionA), plansB(modelB, b, collisionB);
    a.setAngle(0, .6f); a.setAngle(2, .6f); b.setAngle(0, .6f); b.setAngle(2, .6f);
    plansA.update({a.panel(0).center}); plansB.update({b.panel(2).center});
    require(a.present(4) && !a.present(12) && !b.present(4) && b.present(12),
            "Opposite streaming orders must use the same reserved door slots");
    plansA.update({a.panel(0).center, a.panel(2).center});
    plansB.update({b.panel(0).center, b.panel(2).center});
    require(plansA.residentCount() == 2 && plansB.residentCount() == 2 && identical(plansA.renderModel(), plansB.renderModel()),
            "Different building activation orders must converge on identical geometry and draw order");
}
void realMap(const std::filesystem::path& path) {
    auto model = viewer::loadModel(path);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    const auto exteriorCount = doors.count();
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer layer(model, doors, collision);
    std::size_t checked = 0, generated = 0;
    std::set<std::string> buildings;
    for (std::size_t id = 0; id < exteriorCount && generated < 8; ++id) {
        doors.setAngle(id, .6f);
        layer.update({doors.panel(id).center});
        doors.setAngle(id, 0);
        ++checked;
        if (!layer.residentCount()) continue;
        const auto& plan = layer.resident().begin()->second;
        if (!buildings.count(plan.name)) std::cout << plan.name << ": " << area(plan.footprint) << " m2, " << plan.rooms.size()
                  << " ground rooms, " << plan.upperFloors.size() + 1 << " storeys\n";
        if (buildings.insert(plan.name).second) ++generated;
        layer.update({});
    }
    require(generated > 0, "Published map must contain at least one eligible family house");
    std::cout << "Published map: " << generated << " interiors generated from " << checked << " entrance probes\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        streaming(glm::mat4(1));
        streaming(glm::translate(glm::mat4(1), {87, 5, -39}) * glm::rotate(glm::mat4(1), glm::radians(31.f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1}));
        heuristic();
        activationOrder();
        if (argc > 1) realMap(argv[1]);
        std::cout << "Floorplan generation, streaming, collision, and inward door tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
