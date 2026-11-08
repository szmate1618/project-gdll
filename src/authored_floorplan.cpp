#include "authored_floorplan.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <limits>

namespace viewer {
namespace {
void finishes(HouseFloorplan& plan, const AuthoredFloorplan& prepared) {
    Material plaster; plaster.baseColor = {.94f, .91f, .84f, 1}; plaster.doubleSided = true;
    Material trim; trim.baseColor = prepared.institutional ? glm::vec4(.38f, .43f, .45f, 1) : glm::vec4(.65f, .53f, .32f, 1); trim.doubleSided = true;
    const auto ceilingMaterial = static_cast<int>(plan.structure.materials.size());
    plan.structure.materials.push_back(plaster); plan.structure.materials.push_back(trim);
    Primitive ceilings, mouldings; ceilings.material = ceilingMaterial; mouldings.material = ceilingMaterial + 1;
    // Separate white undersides from the wooden walking planes. Reuse the
    // actual triangulation and stair cutouts; never cap a stairwell with trim.
    for (const auto& mesh : plan.structure.primitives) {
        if (mesh.material != 1) continue;
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices[mesh.indices[i]].position;
            const auto b = mesh.vertices[mesh.indices[i + 1]].position;
            const auto c = mesh.vertices[mesh.indices[i + 2]].position;
            if (std::abs(a.y - b.y) > .001f || std::abs(a.y - c.y) > .001f || a.y < plan.floor + .1f) continue;
            const auto base = static_cast<std::uint32_t>(ceilings.vertices.size());
            for (auto p : {a, c, b}) {
                p.y -= .015f; Vertex vertex; vertex.position = p; vertex.normal = {0, -1, 0};
                ceilings.vertices.push_back(vertex);
            }
            ceilings.indices.insert(ceilings.indices.end(), {base, base + 1, base + 2});
        }
    }
    const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        if (glm::length(glm::cross(b - a, c - a)) < .00001f) return;
        const auto base = static_cast<std::uint32_t>(mouldings.vertices.size());
        const auto normal = glm::normalize(glm::cross(b - a, c - a));
        for (const auto p : {a, b, c, d}) { Vertex vertex; vertex.position = p; vertex.normal = normal; mouldings.vertices.push_back(vertex); }
        mouldings.indices.insert(mouldings.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    for (const auto& wall : prepared.layout.partitions) {
        const float floor = plan.floorHeight(wall.storey), ceiling = plan.ceilingHeight(wall.storey);
        for (const float offset : {-.008f, .008f}) {
            const auto point = [&](float along, float y) {
                glm::vec2 p; p[wall.axis] = wall.position + offset; p[1 - wall.axis] = along;
                const auto q = prepared.origin + prepared.u * p.x + prepared.v * p.y;
                return glm::vec3(q.x, y, q.y);
            };
            const auto strip = [&](float low, float high, float bottom, float top) {
                if (high <= low) return;
                quad(point(low, bottom), point(high, bottom), point(high, top), point(low, top));
            };
            strip(wall.low, wall.high, ceiling - .13f, ceiling - .055f);
            float cursor = wall.low;
            for (const auto& door : wall.openings) {
                strip(cursor, door.low, floor + .015f, floor + .13f);
                strip(door.low - .065f, door.low, floor + .015f, floor + door.height + .065f);
                strip(door.high, door.high + .065f, floor + .015f, floor + door.height + .065f);
                strip(door.low, door.high, floor + door.height, floor + door.height + .065f);
                cursor = door.high;
            }
            strip(cursor, wall.high, floor + .015f, floor + .13f);
        }
    }
    for (auto* mesh : {&ceilings, &mouldings}) {
        if (mesh->indices.empty()) continue;
        plan.structure.draws.push_back({plan.structure.primitives.size(), glm::mat4(1), "house_interior_finish"});
        plan.structure.primitives.push_back(std::move(*mesh));
    }
}
}
std::optional<HouseFloorplan> generateAuthoredFloorplan(const AuthoredFloorplan& prepared,
        const HouseSource& source, const std::vector<DoorPanel>& entrances) {
    if (prepared.layout.rooms.empty() || !house_polygon::simple(prepared.footprint)) return std::nullopt;
    for (const auto& room : prepared.layout.rooms) if (!house_polygon::simple(room.polygon)) return std::nullopt;
    HouseFloorplan result;
    result.kind = prepared.version == 1 ? HouseFloorplan::Kind::AuthoredPalace : HouseFloorplan::Kind::AuthoredBuilding;
    result.name = source.name; result.floor = prepared.floor; result.ceiling = prepared.ceiling;
    result.upperFloors = prepared.upperFloors;
    glm::vec2 low(std::numeric_limits<float>::max()), high(-low);
    for (const auto p : prepared.footprint) { low = glm::min(low, p); high = glm::max(high, p); }
    house_layout::buildGeometry(result, source, prepared.layout, prepared.origin, prepared.u, prepared.v,
        {low, high}, prepared.footprint);
    for (const auto& court : prepared.courtyards) {
        std::vector<glm::vec2> points;
        for (const auto p : court) points.push_back(prepared.origin + prepared.u * p.x + prepared.v * p.y);
        result.courtyards.push_back(std::move(points));
    }
    result.structure.materials[0].baseColor = {.88f, .83f, .73f, 1};
    result.structure.materials[1].baseColor = {.40f, .28f, .16f, 1};
    result.structure.materials[2].baseColor = {.78f, .72f, .55f, 1};
    if (prepared.institutional) {
        result.structure.materials[0].baseColor = {.84f, .87f, .85f, 1};
        result.structure.materials[1].baseColor = {.46f, .51f, .52f, 1};
        result.structure.materials[2].baseColor = {.35f, .43f, .48f, 1};
    }
    finishes(result, prepared);
    for (std::size_t i = 0; i < result.rooms.size(); ++i) result.rooms[i].label = prepared.labels.at(i);
    for (const auto id : source.doors) {
        const auto& panel = entrances.at(id);
        const auto point = panel.center - panel.outward * .3f;
        for (auto& room : result.rooms) {
            const float floor = result.floorHeight(room.storey);
            if (panel.center.y < floor || panel.center.y > result.ceilingHeight(room.storey)) continue;
            if (house_polygon::contains(room.polygon, {point.x, point.z})) room.entrances.push_back(id);
        }
    }
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto p : result.footprint) {
        result.minimum = glm::min(result.minimum, {p.x, result.floor, p.y});
        result.maximum = glm::max(result.maximum, {p.x, result.ceiling, p.y});
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
} // namespace viewer
