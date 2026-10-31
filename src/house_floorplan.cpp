#include "house_floorplan.hpp"
#include "house_layout.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace viewer {
namespace {
using Face = std::array<glm::vec3, 3>;
constexpr float tolerance = .08f;
std::vector<Face> faces(const Model& model, const std::vector<std::size_t>& draws) {
    std::vector<Face> result;
    for (const auto index : draws) {
        const auto& draw = model.draws.at(index);
        const auto& mesh = model.primitives.at(draw.primitive);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Face face;
            for (int j = 0; j < 3; ++j)
                face[j] = glm::vec3(draw.transform * glm::vec4(mesh.vertices.at(mesh.indices[i + j]).position, 1));
            result.push_back(face);
        }
    }
    return result;
}
} // namespace

std::optional<HouseFloorplan> generateHouseFloorplan(const Model& model, const HouseSource& source,
                                                   const std::vector<DoorPanel>& entrances) {
    if (source.doors.empty()) return std::nullopt;
    const auto walls = faces(model, source.walls), roof = faces(model, source.roofs);
    if (walls.empty() || roof.empty()) return std::nullopt;
    glm::vec2 origin(walls.front()[0].x, walls.front()[0].z), u(0);
    float longest = 0;
    std::vector<std::pair<glm::vec2, glm::vec2>> segments;
    for (const auto& face : walls) {
        const auto n = glm::cross(face[1] - face[0], face[2] - face[0]);
        if (glm::length(n) < .0001f || std::abs(n.y) > .1f * glm::length(n)) continue;
        for (int j = 0; j < 3; ++j) {
            const glm::vec2 a(face[j].x, face[j].z), b(face[(j + 1) % 3].x, face[(j + 1) % 3].z);
            const auto edge = b - a;
            if (glm::length(edge) < .01f) continue;
            segments.emplace_back(a, b);
            if (glm::dot(edge, edge) > longest) { longest = glm::dot(edge, edge); u = glm::normalize(edge); }
        }
    }
    if (longest < 1) return std::nullopt;
    // Canonical axis signs make the layout independent of wall winding/mirroring.
    if (u.x < -.001f || (std::abs(u.x) <= .001f && u.y < 0)) u = -u;
    const glm::vec2 v(-u.y, u.x);
    const auto local = [&](glm::vec2 p) { const auto d = p - origin; return glm::vec2(glm::dot(d, u), glm::dot(d, v)); };
    const auto world = [&](glm::vec2 p, float y) { const auto q = origin + u * p.x + v * p.y; return glm::vec3(q.x, y, q.y); };
    glm::vec2 low(std::numeric_limits<float>::max()), high(-low);
    for (const auto& edge : segments) for (const auto p : {edge.first, edge.second}) {
        low = glm::min(low, local(p)); high = glm::max(high, local(p));
    }
    const auto size = high - low;
    const float area = size.x * size.y;
    if (area < 45 || area > 220 || std::min(size.x, size.y) < 5 || std::max(size.x, size.y) > 22 ||
        std::max(size.x, size.y) > 2.5f * std::min(size.x, size.y)) return std::nullopt;
    // Every projected wall segment must lie on one rectangle side. Unlike hull
    // fill ratio this rejects holes and small inward recesses as well as L shapes.
    for (const auto& edge : segments) {
        const auto a = local(edge.first), b = local(edge.second);
        bool perimeter = false;
        for (int axis = 0; axis < 2; ++axis) for (const float boundary : {low[axis], high[axis]})
            if (std::abs(a[axis] - boundary) < tolerance && std::abs(b[axis] - boundary) < tolerance) perimeter = true;
        if (!perimeter) return std::nullopt;
    }
    float eaves = std::numeric_limits<float>::max();
    for (const auto& face : roof) for (const auto p : face) eaves = std::min(eaves, p.y);
    std::vector<float> heads;
    for (const auto& face : faces(model, source.windows)) {
        const float bottom = std::min({face[0].y, face[1].y, face[2].y});
        const float top = std::max({face[0].y, face[1].y, face[2].y});
        if (top - bottom > .5f) heads.push_back(top);
    }
    std::sort(heads.begin(), heads.end());
    heads.erase(std::unique(heads.begin(), heads.end(), [](float a, float b) { return std::abs(a - b) < tolerance; }), heads.end());
    float doorHead = std::numeric_limits<float>::max();
    for (const auto id : source.doors) doorHead = std::min(doorHead, entrances.at(id).center.y + entrances.at(id).halfExtents.y);
    // Generated exterior doors and first-row window heads share the same 2.1 m
    // datum above the highest foundation sample. Do not use sloped panel bottoms.
    const float floor = doorHead - 2.1f;
    if ((!heads.empty() && std::abs(heads.front() - doorHead) > .15f) || eaves - floor < 2.4f || eaves - floor > 10) return std::nullopt;
    HouseFloorplan result;
    result.name = source.name; result.floor = floor; result.ceiling = eaves - .02f;
    for (std::size_t i = 1; i < heads.size(); ++i) {
        const float spacing = heads[i] - heads[i - 1];
        if (spacing < 2.4f || spacing > 4.2f) return std::nullopt;
        result.upperFloors.push_back(floor + heads[i] - heads.front());
    }
    const float ceiling = result.upperFloors.empty() ? result.ceiling : result.upperFloors.front();
    if (ceiling - floor < 2.4f) return std::nullopt;
    std::vector<house_layout::Entrance> entryDoors;
    for (const auto id : source.doors) {
        const auto& door = entrances.at(id);
        entryDoors.push_back({id, local({door.center.x, door.center.z}),
            {glm::dot(glm::vec2(door.outward.x, door.outward.z), u),
             glm::dot(glm::vec2(door.outward.x, door.outward.z), v)},
            std::min(1.f, door.halfExtents.x * 2)});
    }
    const auto layout = house_layout::generate(source.name, {low, high}, entryDoors, floor, result.upperFloors);
    if (layout.rooms.empty() || (!result.upperFloors.empty() && layout.stairs.empty())) return std::nullopt;
    house_layout::buildGeometry(result, source, layout, origin, u, v, {low, high});
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto q : {low, glm::vec2(low.x, high.y), high, glm::vec2(high.x, low.y)}) {
        result.minimum = glm::min(result.minimum, world(q, floor));
        result.maximum = glm::max(result.maximum, world(q, result.ceiling));
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
} // namespace viewer
