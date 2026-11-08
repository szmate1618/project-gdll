#include "building_shell.hpp"
#include "house_floorplan.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace viewer::building_shell {
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
std::vector<float> windowHeads(const std::vector<Face>& windows) {
    std::vector<float> heads;
    for (const auto& face : windows) {
        const float bottom = std::min({face[0].y, face[1].y, face[2].y});
        const float top = std::max({face[0].y, face[1].y, face[2].y});
        if (top - bottom > .5f) heads.push_back(top);
    }
    std::sort(heads.begin(), heads.end());
    heads.erase(std::unique(heads.begin(), heads.end(), [](float a, float b) { return std::abs(a - b) < tolerance; }), heads.end());
    return heads;
}
std::optional<house_polygon::Polygon> traceOutline(const std::vector<std::pair<glm::vec2, glm::vec2>>& segments,
                                             glm::vec2 low, glm::vec2 high) {
    std::vector<glm::vec2> points;
    std::set<std::pair<std::size_t, std::size_t>> edges;
    const auto vertex = [&](glm::vec2 p) {
        for (std::size_t i = 0; i < points.size(); ++i) if (glm::distance(points[i], p) < tolerance) return i;
        points.push_back(p); return points.size() - 1;
    };
    for (const auto& [a, b] : segments) {
        const auto direction = glm::normalize(b - a);
        if (std::min(std::abs(direction.x), std::abs(direction.y)) > .087156f) return std::nullopt; // sin(5 degrees).
        const auto first = vertex(a), second = vertex(b);
        if (first != second) edges.emplace(std::min(first, second), std::max(first, second));
    }
    if (points.size() < 4 || points.size() > 64) return std::nullopt;
    std::vector<std::vector<std::size_t>> neighbors(points.size());
    for (const auto& [a, b] : edges) { neighbors[a].push_back(b); neighbors[b].push_back(a); }
    for (const auto& adjacent : neighbors) if (adjacent.size() != 2) return std::nullopt;
    house_polygon::Polygon boundary;
    std::size_t previous = points.size(), current = 0;
    do {
        boundary.push_back(points[current]);
        const auto& adjacent = neighbors[current];
        const auto next = adjacent[0] == previous ? adjacent[1] : adjacent[0];
        previous = current; current = next;
    } while (current != 0 && boundary.size() <= points.size());
    // Multiple cycles include courtyard holes or separate building parts.
    if (current != 0 || boundary.size() != points.size() || !house_polygon::simple(boundary)) return std::nullopt;
    boundary = house_polygon::clip(boundary, low, high);
    const float area = house_polygon::area(boundary), envelopeArea = (high.x - low.x) * (high.y - low.y);
    if (boundary.size() > 24 || area < 45 || area < envelopeArea * .75f) return std::nullopt;
    return boundary;
}

std::optional<Shell> inspect(const Model& model, const HouseSource& source) {
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
    glm::vec2 low(std::numeric_limits<float>::max()), high(-low);
    for (const auto& edge : segments) for (const auto p : {edge.first, edge.second}) {
        low = glm::min(low, local(p)); high = glm::max(high, local(p));
    }
    std::vector<std::pair<glm::vec2, glm::vec2>> localSegments;
    for (const auto& edge : segments) localSegments.emplace_back(local(edge.first), local(edge.second));
    bool rectangular = true;
    for (const auto& [a, b] : localSegments) {
        bool perimeter = false;
        for (int axis = 0; axis < 2; ++axis) for (const float boundary : {low[axis], high[axis]})
            if (std::abs(a[axis] - boundary) < tolerance && std::abs(b[axis] - boundary) < tolerance) perimeter = true;
        rectangular &= perimeter;
    }
    const auto boundary = traceOutline(localSegments, low, high);
    if (!rectangular && !boundary) return std::nullopt;
    Shell result; result.origin = origin; result.u = u; result.v = v; result.low = low; result.high = high;
    result.closed = bool(boundary);
    if (!rectangular) result.outline = *boundary;
    result.eaves = std::numeric_limits<float>::max(); result.roofTop = -result.eaves;
    for (const auto& face : roof) for (const auto p : face) {
        result.eaves = std::min(result.eaves, p.y); result.roofTop = std::max(result.roofTop, p.y);
    }
    std::vector<std::pair<glm::vec2, float>> bases;
    for (const auto& face : walls) for (const auto p : face) {
        const glm::vec2 xz(p.x, p.z);
        const auto found = std::find_if(bases.begin(), bases.end(), [&](const auto& b) { return glm::distance(b.first, xz) < tolerance; });
        if (found == bases.end()) bases.emplace_back(xz, p.y);
        else found->second = std::min(found->second, p.y);
    }
    result.floor = std::numeric_limits<float>::lowest();
    for (const auto& b : bases) result.floor = std::max(result.floor, b.second + .08f);
    for (const auto draw : source.walls)
        if (model.draws.at(draw).buildingFloor) { result.floor = *model.draws.at(draw).buildingFloor; break; }
    return result;
}
} // namespace viewer::building_shell
