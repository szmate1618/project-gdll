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
    const std::string name = "building_relation_3897212_0";
    require(model.authoredFloorplans.count(name) == 1, "Load the station's authored override");
    const auto& prepared = *model.authoredFloorplans.at(name);
    require(prepared.version == 2 && prepared.courtyards.size() == 5, "Read courtyard-aware metadata");
    const auto point = [&](float x, float z, float y) {
        const auto p = prepared.origin + prepared.u * x + prepared.v * z;
        return glm::vec3(p.x, y, p.y);
    };
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    std::size_t entry = doors.count(), count = 0;
    float nearest = 1e9f;
    for (std::size_t i = 0; i < doors.count(); ++i) {
        const auto& panel = doors.panel(i);
        if (model.draws.at(panel.draw).name != name + "_doors") continue;
        const auto d = glm::distance(panel.center, point(25, 3.35f, prepared.floor + 1.1f));
        if (d < nearest) { nearest = d; entry = i; }
        ++count;
    }
    require(count == 4 && nearest < .2f, "Replace generic exterior doors with the four authored entrances");
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, collision);
    doors.setAngle(entry, 1.5f);
    const auto entryPosition = doors.panel(entry).center;
    floors.update({entryPosition});
    const auto& plan = floors.resident().at(name);
    require(plan.kind == viewer::HouseFloorplan::Kind::AuthoredBuilding && plan.upperFloors.size() == 3,
            "Station overrides house and prefab heuristics with four storeys");
    require(plan.stairs.size() == 6 && plan.courtyards.size() == 5, "Retain two stair cores and all five courts");
    for (const auto& door : plan.doors) {
        require(doors.present(door.id), "Install every reserved interior door");
        require(door.panel.halfExtents.y > 1 && door.panel.halfExtents.y < 1.1f, "Institutional doors have normal head heights");
    }
    for (const auto& court : plan.courtyards) {
        glm::vec2 center(0);
        for (const auto p : court) center += p / float(court.size());
        require(!plan.contains(center), "Courtyard is outside the interior shell");
        // Published terrain can lie well below the building's highest-ground
        // floor datum. Query only the volume where a spurious slab would be.
        const auto ground = collision.groundAt(center.x, center.y, plan.floor + .02f, plan.ceiling + .1f);
        require(!ground, "Never cover a courtyard with a generated upper floor or ceiling");
    }
    for (const auto& stair : plan.stairs) for (const float t : {.05f, .3f, .6f, .95f}) {
        const auto p = glm::mix(stair.bottom, stair.top, t);
        const auto ground = collision.groundAt(p.x, p.z, p.y - .04f, p.y + .04f);
        require(ground && std::abs(ground->height - p.y) < .02f, "Ramp provides continuous walking support");
        require(collision.lineOfSight(p + glm::vec3(0, .1f, 0), p + glm::vec3(0, 1.9f, 0)),
                "Incoming cutouts and alternating flights preserve standing head clearance");
    }
    const auto* graph = floors.navigationGraph(name);
    require(graph != nullptr, "Install station room navigation");
    const auto lobby = point(26, 10, plan.floorHeight(0));
    std::size_t disconnected = 0;
    for (const auto& node : graph->nodes()) {
        if (node.kind != viewer::HouseNavigationGraph::Kind::RoomCenter) continue;
        const auto route = graph->route(lobby, node.point);
        if (route.points.empty() || glm::distance(route.points.back().point, node.point) > 2) {
            ++disconnected;
            const auto room = graph->roomAt(node.point);
            std::cerr << "Disconnected: " << (room ? plan.rooms[*room].label : "unknown") << ' '
                      << node.point.x << ',' << node.point.y << ',' << node.point.z << '\n';
        }
    }
    require(disconnected == 0, "Every station room on every storey must connect to reception");
    for (const auto& door : plan.doors) doors.setAngle(door.id, -1.55f);
    doors.setAngle(entry, -1.55f);
    const auto panel = doors.panel(entry);
    const auto approach = panel.center + panel.outward * 8.f;
    const auto ground = collision.groundAt(approach.x, approach.z, plan.floor - 15, plan.floor + .02f);
    require(bool(ground), "Entrance approach has ground support");
    viewer::FPSController player;
    player.teleport({approach.x, ground->height + .003f, approach.z});
    viewer::FPSInput input; input.forward = 1;
    input.yawDegrees = glm::degrees(std::atan2(-panel.outward.z, -panel.outward.x));
    for (int step = 0; step < 700; ++step) player.update(1.f / 120, input, collision);
    if (glm::dot(player.feet() - panel.center, panel.outward) >= -.7f)
    {
        std::cerr << "Entry walk stopped at " << player.feet().x << ',' << player.feet().y << ',' << player.feet().z
                  << "; door " << panel.center.x << ',' << panel.center.y << ',' << panel.center.z
                  << "; outward " << panel.outward.x << ',' << panel.outward.z << '\n';
        for (const auto& contact : collision.contacts(player.feet(), .35f, 1.8f, .03f)) {
            const auto q = glm::vec2(contact.point.x, contact.point.z) - prepared.origin;
            std::cerr << "Contact local " << glm::dot(q, prepared.u) << ',' << contact.point.y << ',' << glm::dot(q, prepared.v)
                      << " normal " << contact.normal.x << ',' << contact.normal.y << ',' << contact.normal.z
                      << " terrain " << contact.terrain << " door " << contact.door << '\n';
        }
    }
    require(glm::dot(player.feet() - panel.center, panel.outward) < -.7f, "Player can walk from terrain through the main entrance");
    const auto id = plan.doors.front().id;
    floors.update({entryPosition + glm::vec3(1000, 0, 1000)});
    require(!doors.present(id), "Evict station panels with the structure");
    floors.update({entryPosition});
    require(floors.resident().at(name).doors.front().id == id, "Station door IDs survive re-entry");
    std::cout << "Station: " << prepared.layout.rooms.size() << " rooms, four storeys, five open courts, walkable stairs and stable doors\n";
}
}
int main(int argc, char** argv) {
    try { check(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(STATION_FIXTURE)); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
