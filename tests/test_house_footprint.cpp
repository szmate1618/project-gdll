#include "house_fixture.hpp"
#include "house_polygon.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"
#include "interior_zombies.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
// Independent winding-number check, also used at triangle interiors so a floor
// that merely gets the right total area cannot bridge an exterior notch.
bool inside(const std::vector<glm::vec2>& polygon, glm::vec2 p) {
    int winding = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (a.y <= p.y && b.y > p.y && cross > 0) ++winding;
        if (a.y > p.y && b.y <= p.y && cross < 0) --winding;
    }
    return winding != 0;
}
void polygonOperations() {
    using namespace viewer::house_polygon;
    const Polygon u{{0, 0}, {0, 6}, {2, 6}, {2, 2}, {4, 2}, {4, 6}, {6, 6}, {6, 0}};
    require(simple(u) && area(u) == 28, "Concave boundary area must exclude its notch");
    require(contains(u, {1, 4}) && !contains(u, {3, 4}), "Containment must distinguish the two sides of a concavity");
    require(clip(u, {0, 3}, {6, 6}).empty(), "Disconnected clipped rooms must not be joined by a zero-width bridge");
    const auto indices = triangles(u);
    float sum = 0;
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto a = u[indices[i]], b = u[indices[i + 1]], c = u[indices[i + 2]];
        require(inside(u, (a + b + c) / 3.f), "Triangulation must never cover a notch");
        sum += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * .5f;
    }
    require(sum == 28, "Triangulation must preserve the true polygon area");
    require(!covers(u, {1, 3}, {5, 4}), "Staircase fit must reject a rectangle that crosses exterior space");
    const auto intervals = spans(u, 1, 4, 0, 6);
    require(intervals.size() == 2 && intervals[0] == std::pair<float, float>(0, 2) && intervals[1] == std::pair<float, float>(4, 6),
            "A wall crossing a recess must be clipped into separate interior spans");
}
void generated(const std::vector<glm::vec2>& outline, const glm::mat4& transform, int levels) {
    auto model = house_fixture::outlined(outline, transform, levels);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, collision);
    require(doors.count() >= 2, "Irregular fixture must retain its front and back doors");
    doors.setAngle(0, .7f); floors.update({doors.panel(0).center});
    require(floors.residentCount() == 1, "A modest near-orthogonal footprint must generate an interior");
    const auto plan = floors.resident().begin()->second;
    float area = 0;
    for (const auto& room : plan.rooms) {
        require(room.polygon.size() >= 3 && viewer::house_polygon::simple(room.polygon), "Every clipped room must remain a single simple polygon");
        area += viewer::house_polygon::area(room.polygon);
    }
    require(std::abs(area - viewer::house_polygon::area(plan.footprint) * levels) < .025f,
            "Clipped rooms must tile every storey of the actual footprint");
    const auto inverse = glm::inverse(transform);
    const auto local = [&](glm::vec3 p) { return glm::vec3(inverse * glm::vec4(p, 1)); };
    for (const auto& mesh : plan.structure.primitives) for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = local(mesh.vertices[mesh.indices[i]].position), b = local(mesh.vertices[mesh.indices[i + 1]].position), c = local(mesh.vertices[mesh.indices[i + 2]].position);
        for (const auto weights : {glm::vec3(.34f, .33f, .33f), {.8f, .1f, .1f}, {.1f, .8f, .1f}, {.1f, .1f, .8f}}) {
            const auto p = a * weights.x + b * weights.y + c * weights.z;
            // Vertical walls may meet the exact shell boundary; horizontal and
            // ramp interiors must be strictly inside the source outline.
            if (std::abs(glm::cross(b - a, c - a).y) > .001f)
                require(inside(outline, {p.x, p.z}), "Generated floors, ceilings and ramps must not bridge missing building corners");
        }
    }
    require(plan.stairs.size() == std::size_t(levels - 1), "Irregular houses must retain one safe flight per upper level");
    for (int level = 0; level < levels; ++level) for (float x = -4.75f; x < 5; x += .5f) for (float z = -5.75f; z < 6; z += .5f) {
        const float height = level == 0 ? .01f : float(level) * 3;
        const auto p = glm::vec3(transform * glm::vec4(x, height, z, 1));
        bool hole = false;
        for (const auto& stair : plan.stairs)
            if (std::abs(stair.top.y - p.y) < .01f && inside(stair.opening, {p.x, p.z})) hole = true;
        const auto ground = collision.walkingGroundAt(p.x, p.z, p.y - .004f, p.y + .004f);
        if (inside(outline, {x, z}) && !hole)
            require(ground && ground->normal.y > .99f, "Every interior floor cell outside a stair opening must provide walking support");
        else if (!inside(outline, {x, z})) require(!ground, "Exterior recesses must never receive generated walking support");
    }
    for (const auto& door : plan.doors) for (const float sign : {-1.f, 1.f}) {
        const auto p = local(door.panel.center + sign * door.panel.outward * .25f);
        require(inside(outline, {p.x, p.z}), "Both sides of every generated doorway must stay inside the real house");
    }
    for (int seed = 0; seed < 50; ++seed) {
        auto encounter = plan; encounter.name = "irregular_encounter_" + std::to_string(seed);
        for (const auto& actor : viewer::placeInteriorZombies(encounter, doors, collision)) {
            const auto p = local(glm::vec3(actor.transform[3]));
            require(inside(outline, {p.x, p.z}), "Interior zombies must stay inside concave rooms");
        }
    }
    floors.update({}); doors.setAngle(1, .7f); floors.update({doors.panel(1).center});
    const auto& repeated = floors.resident().begin()->second;
    require(repeated.footprint == plan.footprint && repeated.rooms.size() == plan.rooms.size() && repeated.doors.size() == plan.doors.size(),
            "Irregular interiors must regenerate identically through either entrance");
    for (std::size_t i = 0; i < plan.structure.primitives.size(); ++i) {
        const auto& a = plan.structure.primitives[i]; const auto& b = repeated.structure.primitives[i];
        require(a.indices == b.indices && a.vertices.size() == b.vertices.size(), "Regeneration must retain irregular topology");
        for (std::size_t j = 0; j < a.vertices.size(); ++j) require(a.vertices[j].position == b.vertices[j].position, "Regeneration must retain exact irregular coordinates");
    }
}
void rejected(const std::vector<glm::vec2>& outline) {
    auto model = house_fixture::outlined(outline, glm::mat4(1), 1);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, collision);
    doors.setAngle(0, .7f); floors.update({doors.panel(0).center});
    require(floors.residentCount() == 0, "Strongly diagonal or highly indented footprints must remain ineligible");
}
} // namespace
int main() {
    try {
        polygonOperations();
        const auto moved = glm::translate(glm::mat4(1), {-1108, 5, -250}) * glm::rotate(glm::mat4(1), glm::radians(31.f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1});
        for (const auto& outline : std::vector<std::vector<glm::vec2>>{
             {{-5,-6},{-5,6},{2,6},{2,3},{5,3},{5,-6}}, // Corner cutout.
             {{-5,-6},{-5,6},{-1,6},{-1,4},{1,4},{1,6},{5,6},{5,-6}}, // Recessed entrance.
             {{-5,-6},{-5,4},{-2,4},{-2,6},{2,6},{2,4},{5,4},{5,-6}}, // Small extension.
             {{-5,-6},{-5,6},{5.25f,6},{5,-6}}}) { // Slightly skew wall.
            for (const auto transform : {glm::mat4(1), moved}) for (const int levels : {1, 2, 3}) generated(outline, transform, levels);
        }
        rejected({{-5,-6},{-5,6},{-1,6},{-1,-1},{5,-1},{5,-6}});
        rejected({{-5,-6},{-5,6},{5,6},{2,-6}});
        std::cout << "Irregular footprint, clipping, rooms, staircases and regeneration tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
