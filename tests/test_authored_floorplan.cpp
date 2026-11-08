#include "authored_floorplan.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "house_navigation.hpp"
#include "house_polygon.hpp"
#include "physics_world.hpp"
#include "fps_controller.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void check(const std::filesystem::path& path) {
    auto model = viewer::loadModel(path);
    const std::string palace = "building_way_107918884_0";
    require(model.authoredFloorplans.count(palace) == 1, "Load the authored palace interior from wall metadata");
    const auto& prepared = *model.authoredFloorplans.at(palace);
    require(viewer::house_polygon::simple(prepared.footprint), "Authored footprint must survive float conversion");
    for (std::size_t i = 0; i < prepared.layout.rooms.size(); ++i)
        if (!viewer::house_polygon::simple(prepared.layout.rooms[i].polygon))
            throw std::runtime_error("Invalid authored room polygon: " + std::to_string(i) + " " + prepared.labels[i]);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    const auto exteriorCount = doors.count();
    std::size_t entryId = exteriorCount, palaceDoors = 0;
    const auto front = prepared.origin + prepared.u * 27.8f - prepared.v * 6.7f;
    float nearest = std::numeric_limits<float>::max();
    bool courtyardExit = false;
    for (std::size_t i = 0; i < exteriorCount; ++i) {
        if (model.draws.at(doors.panel(i).draw).name.rfind(palace + "_doors", 0)) continue;
        const auto p = doors.panel(i).center;
        const auto local = glm::vec2(p.x, p.z) - prepared.origin;
        courtyardExit |= std::abs(glm::dot(local, prepared.u) - 27.8f) < 3 &&
            std::abs(glm::dot(local, prepared.v) - 24.7f) < .3f;
        const float distance = glm::distance(front, glm::vec2(p.x, p.z));
        if (distance < nearest) { nearest = distance; entryId = i; }
        ++palaceDoors;
    }
    require(entryId < exteriorCount && palaceDoors > 8, "Palace must replace the two generic exterior doors");
    require(courtyardExit, "The entrance hall must retain its documented courtyard exit despite the OSM facade bend");
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer layer(model, doors, collision);
    const auto entry = doors.panel(entryId).center;
    doors.setAngle(entryId, .9f); layer.update({entry});
    require(layer.residentCount() == 1, "Opening a palace exterior door must stream the authored floorplan");
    const auto& plan = layer.resident().begin()->second;
    require(plan.kind == viewer::HouseFloorplan::Kind::AuthoredPalace && plan.upperFloors.size() == 1,
        "Palace has a complete ground and first floor");
    require(plan.stairs.size() == 2 && plan.rooms.size() > 100 && plan.doors.size() > 100,
        "Palace must retain both stairs, main-body rooms and rear-wing rooms");
    for (const auto& door : plan.doors) {
        require(doors.present(door.id), "Every authored interior panel must have an installed stable ID");
        require(door.panel.halfExtents.y > 1.3f, "Palace interior doorways use their taller head height");
    }
    for (const auto& stair : plan.stairs) {
        const auto direction = glm::normalize(stair.top - stair.bottom);
        for (const float t : {.1f, .5f, .9f}) {
            const auto point = glm::mix(stair.bottom, stair.top, t);
            const auto ground = collision.groundAt(point.x, point.z, point.y - .05f, point.y + .05f);
            require(ground && std::abs(ground->height - point.y) < .02f, "Stair ramp must provide continuous walking support");
            require(collision.lineOfSight(point + glm::vec3(0, .1f, 0), point + glm::vec3(0, 1.9f, 0)),
                "Upper floor cutout must preserve standing head clearance over each ramp");
        }
        (void)direction;
    }
    const auto* graph = layer.navigationGraph(plan.name);
    require(graph && !graph->nodes().empty(), "Authored palace installs its interior navigation graph");
    const auto point = [&](float x, float y, float height) {
        const auto p = prepared.origin + prepared.u * x + prepared.v * y;
        return glm::vec3(p.x, height, p.y);
    };
    const auto lobby = point(27.8f, 10, plan.floorHeight(0));
    const auto hall = point(27.8f, 20, plan.floorHeight(1));
    const auto route = graph->route(lobby, hall);
    require(route.room && !route.points.empty() && glm::distance(route.points.back().point, hall) < 2,
        "Navigation must connect the entrance hall to the first-floor ceremonial hall");
    std::size_t disconnected = 0;
    for (const auto& node : graph->nodes()) {
        if (node.kind != viewer::HouseNavigationGraph::Kind::RoomCenter) continue;
        const auto path = graph->route(lobby, node.point);
        if (path.points.empty() || glm::distance(path.points.back().point, node.point) > 2) {
            ++disconnected;
            const auto room = graph->roomAt(node.point);
            std::cerr << "Disconnected room center " << node.point.x << ',' << node.point.y << ',' << node.point.z
                      << " " << (room ? plan.rooms[*room].label : "unknown") << '\n';
        }
    }
    require(disconnected == 0, "Every navigable palace room must connect to the entrance hall");
    for (const auto& door : plan.doors) doors.setAngle(door.id, -1.55f);
    doors.setAngle(entryId, -1.55f);
    const auto panel = doors.panel(entryId);
    const auto approach = panel.center + panel.outward * 14.f;
    const auto support = collision.groundAt(approach.x, approach.z, plan.floor - 15, plan.floor + .02f);
    require(bool(support), "Palace entrance approach must have ground support");
    viewer::FPSController player;
    player.teleport({approach.x, support->height + .003f, approach.z});
    viewer::FPSInput input; input.forward = 1;
    input.yawDegrees = glm::degrees(std::atan2(-panel.outward.z, -panel.outward.x));
    for (int step = 0; step < 1000; ++step) player.update(1.f / 120, input, collision);
    require(glm::dot(player.feet() - panel.center, panel.outward) < -.8f,
        "A walking player must cross the replacement main entrance from the terrain into the palace");
    const auto courtyard = point(27.8f, 35, plan.floorHeight(1));
    require(!viewer::house_polygon::contains(plan.footprint, {courtyard.x, courtyard.z}), "Preserve the open palace courtyard");
    const auto ids = plan.doors.front().id;
    layer.update({entry + glm::vec3(1000, 0, 1000)});
    require(layer.residentCount() == 0 && !doors.present(ids), "Palace streaming must evict geometry and panels together");
    layer.update({entry});
    require(layer.residentCount() == 1 && layer.resident().begin()->second.doors.front().id == ids,
        "Palace interior door IDs must survive eviction and re-entry");
    std::cout << "Authored palace: " << prepared.layout.rooms.size() << " rooms, stable doors, two floors and navigable stairs\n";
}
}
int main(int argc, char** argv) {
    try { check(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(PALACE_FIXTURE)); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
