#include "house_layout.hpp"
#include "house_polygon.hpp"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace viewer::house_layout {
namespace {
void quad(Primitive& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    if (glm::length(glm::cross(b - a, c - a)) < .0001f) return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto normal = glm::normalize(glm::cross(b - a, c - a));
    for (const auto p : {a, b, c, d}) { Vertex v; v.position = p; v.normal = normal; mesh.vertices.push_back(v); }
    for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
}
std::array<glm::vec2, 4> corners(Rectangle r) {
    return {{r.low, {r.low.x, r.high.y}, r.high, {r.high.x, r.low.y}}};
}
} // namespace
void buildGeometry(HouseFloorplan& result, const HouseSource& source, const Layout& layout,
                   glm::vec2 origin, glm::vec2 u, glm::vec2 v, Rectangle footprint,
                   const std::vector<glm::vec2>& outline) {
    const auto world = [&](glm::vec2 p, float y) { const auto q = origin + u * p.x + v * p.y; return glm::vec3(q.x, y, q.y); };
    const auto polygon = [&](Rectangle rect) {
        std::vector<glm::vec2> output;
        for (const auto p : corners(rect)) { const auto q = world(p, 0); output.emplace_back(q.x, q.z); }
        return output;
    };
    const auto envelope = polygon(footprint);
    std::copy(envelope.begin(), envelope.end(), result.envelope.begin());
    result.footprint = envelope;
    if (!outline.empty()) {
        result.footprint.clear();
        for (const auto p : outline) { const auto q = world(p, 0); result.footprint.emplace_back(q.x, q.z); }
    }
    result.structure.materials.resize(3);
    result.structure.materials[0].baseColor = {.68f, .65f, .58f, 1};
    result.structure.materials[1].baseColor = {.36f, .30f, .23f, 1};
    result.structure.materials[2].baseColor = {.38f, .22f, .10f, 1};
    for (auto& material : result.structure.materials) material.doubleSided = true;
    Primitive walls, floors, ramps; walls.material = 0; floors.material = 1; ramps.material = 1;
    std::vector<glm::vec2> shellVertices;
    const auto appendTriangles = [&](const std::vector<glm::vec2>& region) {
        for (const auto index : house_polygon::triangles(region)) shellVertices.push_back(region[index]);
    };
    if (layout.floorRegions.empty()) appendTriangles(outline);
    else for (const auto& region : layout.floorRegions) appendTriangles(region);
    const auto plane = [&](Rectangle r, float y) {
        if (r.high.x <= r.low.x || r.high.y <= r.low.y) return;
        if (!outline.empty()) {
            // A slab around a stair opening can intersect a concave house in
            // several pieces. Clip each shell triangle, never bridge the void.
            for (std::size_t i = 0; i < shellVertices.size(); i += 3) {
                const auto clipped = house_polygon::clip({shellVertices[i], shellVertices[i + 1], shellVertices[i + 2]}, r.low, r.high);
                const auto base = static_cast<std::uint32_t>(floors.vertices.size());
                for (const auto p : clipped) { Vertex vertex; vertex.position = world(p, y); vertex.normal = {0, 1, 0}; floors.vertices.push_back(vertex); }
                for (std::uint32_t j = 1; j + 1 < clipped.size(); ++j)
                    floors.indices.insert(floors.indices.end(), {base, base + j, base + j + 1});
            }
            return;
        }
        const auto p = corners(r);
        quad(floors, world(p[0], y), world(p[1], y), world(p[2], y), world(p[3], y));
    };
    const auto floorPlane = [&](Rectangle bounds, float y) {
        // Cut every incoming flight out of this slab, including the basement
        // flight through the ground floor and both bays of apartment stairs.
        std::vector<Rectangle> pieces{bounds};
        for (const auto& stair : layout.stairs) {
            if (std::abs(stair.top - y) > .02f) continue;
            std::vector<Rectangle> remaining;
            for (const auto& piece : pieces) {
                const Rectangle hole{glm::max(piece.low, stair.opening.low), glm::min(piece.high, stair.opening.high)};
                if (hole.low.x >= hole.high.x || hole.low.y >= hole.high.y) { remaining.push_back(piece); continue; }
                remaining.push_back({piece.low, {hole.low.x, piece.high.y}});
                remaining.push_back({{hole.high.x, piece.low.y}, piece.high});
                remaining.push_back({{hole.low.x, piece.low.y}, {hole.high.x, hole.low.y}});
                remaining.push_back({{hole.low.x, hole.high.y}, {hole.high.x, piece.high.y}});
            }
            pieces = std::move(remaining);
        }
        for (const auto& piece : pieces) plane(piece, y);
    };
    for (const auto& room : layout.rooms) {
        auto boundary = polygon(room.bounds);
        if (!room.polygon.empty()) {
            boundary.clear();
            for (const auto p : room.polygon) { const auto q = world(p, 0); boundary.emplace_back(q.x, q.z); }
        } else if (!outline.empty()) {
            boundary.clear();
            for (const auto p : house_polygon::clip(outline, room.bounds.low, room.bounds.high)) {
                const auto q = world(p, 0); boundary.emplace_back(q.x, q.z);
            }
            if (boundary.empty()) throw std::logic_error("House room became disconnected or empty after clipping");
        }
        result.rooms.push_back({room.kind, std::move(boundary), room.entrances,
            room.kind == HouseRoom::Kind::StairArea, room.storey});
        if (room.storey == 0 && !layout.wholeGroundFloor) floorPlane(room.bounds, result.floor + .01f);
    }
    if (layout.wholeGroundFloor) floorPlane(footprint, result.floor + .01f);
    if (result.basementFloor) plane(footprint, *result.basementFloor + .01f);
    for (const auto& wall : layout.partitions) {
        const float floor = result.floorHeight(wall.storey) - (wall.storey == 0 ? .01f : 0.f);
        const float ceiling = result.ceilingHeight(wall.storey);
        const auto point = [&](float along, float y) {
            glm::vec2 p; p[wall.axis] = wall.position; p[1 - wall.axis] = along; return world(p, y);
        };
        auto openings = wall.openings;
        std::sort(openings.begin(), openings.end(), [](const Opening& a, const Opening& b) { return a.low < b.low; });
        const auto spans = outline.empty() ? std::vector<std::pair<float, float>>{{wall.low, wall.high}} :
            house_polygon::spans(outline, wall.axis, wall.position, wall.low, wall.high);
        for (const auto& span : spans) {
            float cursor = span.first;
            for (const auto& opening : openings) {
                if (opening.high <= span.first || opening.low >= span.second) continue;
                if (opening.low < cursor || opening.high > span.second) throw std::logic_error("Overlapping or clipped house doorways");
                const float head = std::min(floor + opening.height, ceiling);
                quad(walls, point(cursor, floor), point(opening.low, floor), point(opening.low, ceiling), point(cursor, ceiling));
                quad(walls, point(opening.low, head), point(opening.high, head), point(opening.high, ceiling), point(opening.low, ceiling));
                const auto normal = u * opening.outward.x + v * opening.outward.y;
                const glm::vec3 outward(normal.x, 0, normal.y), up(0, 1, 0);
                const auto right = glm::cross(up, outward);
                DoorPanel panel;
                panel.draw = std::numeric_limits<std::size_t>::max(); panel.inward = opening.inward;
                panel.center = (point(opening.low, floor) + point(opening.high, head)) * .5f;
                panel.center.y = (floor + .03f + head) * .5f;
                panel.halfExtents = {(opening.high - opening.low) * .5f, (head - floor - .03f) * .5f, .02f};
                panel.hinge = panel.center - right * panel.halfExtents.x; panel.outward = outward;
                panel.rotation = glm::quat_cast(glm::mat3(right, up, outward));
                Primitive mesh; mesh.material = 2;
                const auto a = panel.center - right * panel.halfExtents.x, b = panel.center + right * panel.halfExtents.x;
                quad(mesh, {a.x, floor + .03f, a.z}, {b.x, floor + .03f, b.z}, {b.x, head, b.z}, {a.x, head, a.z});
                result.doors.push_back({source.firstDoor + result.doors.size(), opening.entrance, panel, std::move(mesh), wall.storey});
                cursor = opening.high;
            }
            quad(walls, point(cursor, floor), point(span.second, floor), point(span.second, ceiling), point(cursor, ceiling));
        }
    }
    for (const auto y : result.upperFloors) floorPlane(footprint, y);
    plane(footprint, result.ceiling);
    for (const auto& stair : layout.stairs) {
        const auto height = [&](glm::vec2 p) {
            float t = (p[stair.axis] - stair.ramp.low[stair.axis]) / (stair.ramp.high[stair.axis] - stair.ramp.low[stair.axis]);
            if (!stair.ascending) t = 1 - t;
            return glm::mix(stair.bottom, stair.top, t);
        };
        const auto p = corners(stair.ramp);
        quad(ramps, world(p[0], height(p[0])), world(p[1], height(p[1])), world(p[2], height(p[2])), world(p[3], height(p[3])));
        auto bottom = (stair.ramp.low + stair.ramp.high) * .5f, top = bottom;
        bottom[stair.axis] = stair.ascending ? stair.ramp.low[stair.axis] : stair.ramp.high[stair.axis];
        top[stair.axis] = stair.ascending ? stair.ramp.high[stair.axis] : stair.ramp.low[stair.axis];
        result.stairs.push_back({stair.room, polygon(stair.ramp), polygon(stair.opening), world(bottom, stair.bottom), world(top, stair.top)});
        result.rooms.at(stair.room).staircase = true;
    }
    // A reflected authored frame reverses local X/Z winding. Walking support
    // still needs upward-facing floors/ramps, independently of wall materials.
    if (u.x * v.y - u.y * v.x < 0) {
        for (auto* mesh : {&floors, &ramps}) {
            for (std::size_t i = 0; i < mesh->indices.size(); i += 3)
                std::swap(mesh->indices[i + 1], mesh->indices[i + 2]);
            for (auto& vertex : mesh->vertices) if (vertex.normal.y < 0) vertex.normal = -vertex.normal;
        }
    }
    for (auto* primitive : {&walls, &floors, &ramps}) {
        if (primitive->indices.empty()) continue;
        result.structure.draws.push_back({result.structure.primitives.size(), glm::mat4(1), "house_interior"});
        result.structure.primitives.push_back(std::move(*primitive));
    }
}
} // namespace viewer::house_layout
