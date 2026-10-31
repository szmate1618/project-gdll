#include "house_floorplan.hpp"

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
void quad(Primitive& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    if (glm::length(glm::cross(b - a, c - a)) < .0001f) return;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto normal = glm::normalize(glm::cross(b - a, c - a));
    for (const auto p : {a, b, c, d}) {
        Vertex v; v.position = p; v.normal = normal;
        mesh.vertices.push_back(v);
    }
    for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
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
    result.structure.materials.resize(3);
    result.structure.materials[0].baseColor = {.68f, .65f, .58f, 1};
    result.structure.materials[1].baseColor = {.36f, .30f, .23f, 1};
    result.structure.materials[2].baseColor = {.38f, .22f, .10f, 1};
    for (auto& material : result.structure.materials) material.doubleSided = true;
    Primitive partitions, floors; partitions.material = 0; floors.material = 1;
    const auto polygon = [&](glm::vec2 a, glm::vec2 b) {
        std::vector<glm::vec2> p;
        for (const auto q : {a, glm::vec2(a.x, b.y), b, glm::vec2(b.x, a.y)}) {
            const auto w = world(q, floor); p.emplace_back(w.x, w.z);
        }
        return p;
    };
    result.footprint = polygon(low, high);
    const auto plane = [&](glm::vec2 a, glm::vec2 b, float y) {
        quad(floors, world(a, y), world({a.x, b.y}, y), world(b, y), world({b.x, a.y}, y));
    };
    // Group coplanar entrances: two exterior doors on one facade share an entry
    // polygon, but receive their own aligned openings and interactive panels.
    std::map<int, std::vector<std::size_t>> sides;
    for (const auto id : source.doors) {
        const auto& door = entrances.at(id);
        const auto p = local({door.center.x, door.center.z});
        const glm::vec2 normal(glm::dot(glm::vec2(door.outward.x, door.outward.z), u),
                               glm::dot(glm::vec2(door.outward.x, door.outward.z), v));
        const int axis = std::abs(normal.x) > std::abs(normal.y) ? 0 : 1;
        const bool positive = normal[axis] > 0;
        if (std::abs(normal[axis]) < .99f || std::abs(p[axis] - (positive ? high[axis] : low[axis])) > .15f) return std::nullopt;
        sides[axis * 2 + int(positive)].push_back(id);
    }
    auto livingLow = low, livingHigh = high;
    for (const auto& side : sides) {
        const int axis = side.first / 2, along = 1 - axis;
        const bool positive = side.first % 2;
        auto entryLow = livingLow, entryHigh = livingHigh;
        const float cut = positive ? high[axis] - 2 : low[axis] + 2;
        if (positive) { entryLow[axis] = cut; livingHigh[axis] = cut; }
        else { entryHigh[axis] = cut; livingLow[axis] = cut; }
        if (livingHigh[axis] - livingLow[axis] < 1 || entryHigh[along] - entryLow[along] < 2) return std::nullopt;
        result.rooms.push_back({HouseRoom::Kind::Entry, polygon(entryLow, entryHigh), side.second});
        plane(entryLow, entryHigh, floor + .01f);
        struct Opening { float low, high, top; std::size_t id; };
        std::vector<Opening> openings;
        for (const auto id : side.second) {
            const auto& exterior = entrances[id];
            const auto p = local({exterior.center.x, exterior.center.z});
            const float width = std::min(1.0f, 2 * exterior.halfExtents.x);
            const float left = p[along] - width * .5f, right = p[along] + width * .5f;
            if (left < entryLow[along] + .12f || right > entryHigh[along] - .12f) return std::nullopt;
            openings.push_back({left, right, floor + 2.1f, id});
        }
        std::sort(openings.begin(), openings.end(), [](const Opening& a, const Opening& b) { return a.low < b.low; });
        const auto point = [&](float x, float y) { glm::vec2 p; p[axis] = cut; p[along] = x; return world(p, y); };
        float cursor = entryLow[along];
        for (const auto& opening : openings) {
            if (opening.low < cursor) return std::nullopt;
            quad(partitions, point(cursor, floor), point(opening.low, floor), point(opening.low, ceiling), point(cursor, ceiling));
            quad(partitions, point(opening.low, opening.top), point(opening.high, opening.top), point(opening.high, ceiling), point(opening.low, ceiling));
            const auto outward = entrances[opening.id].outward;
            const auto right = glm::cross(glm::vec3(0, 1, 0), outward);
            const auto center = (point(opening.low, floor) + point(opening.high, opening.top)) * .5f;
            DoorPanel panel;
            panel.draw = std::numeric_limits<std::size_t>::max(); panel.inward = true;
            panel.outward = outward; panel.center = center;
            panel.halfExtents = {(opening.high - opening.low) * .5f, 1.035f, .02f};
            panel.center.y = floor + 1.065f; // 3 cm ground clearance; head stays at 2.1 m.
            panel.hinge = panel.center - right * panel.halfExtents.x;
            panel.rotation = glm::quat_cast(glm::mat3(right, glm::vec3(0, 1, 0), outward));
            Primitive mesh; mesh.material = 2;
            const auto a = center - right * panel.halfExtents.x;
            const auto b = center + right * panel.halfExtents.x;
            quad(mesh, {a.x, floor + .03f, a.z}, {b.x, floor + .03f, b.z}, {b.x, opening.top, b.z}, {a.x, opening.top, a.z});
            result.doors.push_back({opening.id, panel, std::move(mesh)});
            cursor = opening.high;
        }
        quad(partitions, point(cursor, floor), point(entryHigh[along], floor), point(entryHigh[along], ceiling), point(cursor, ceiling));
    }
    result.rooms.push_back({HouseRoom::Kind::Living, polygon(livingLow, livingHigh), {}});
    // Lift ground-storey surfaces 1 cm above the existing terrain datum to
    // avoid coplanar depth fighting without changing window-derived storeys.
    plane(livingLow, livingHigh, floor + .01f);
    for (const auto y : result.upperFloors) plane(low, high, y);
    plane(low, high, result.ceiling);
    result.structure.primitives.push_back(std::move(partitions));
    result.structure.primitives.push_back(std::move(floors));
    for (std::size_t i = 0; i < result.structure.primitives.size(); ++i)
        result.structure.draws.push_back({i, glm::mat4(1), "house_interior"});
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto q : {low, glm::vec2(low.x, high.y), high, glm::vec2(high.x, low.y)}) {
        result.minimum = glm::min(result.minimum, world(q, floor));
        result.maximum = glm::max(result.maximum, world(q, result.ceiling));
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
} // namespace viewer
