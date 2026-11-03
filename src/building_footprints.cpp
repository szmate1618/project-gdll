#include "building_footprints.hpp"
#include "house_polygon.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace viewer {
namespace {
constexpr float weld = .01f;
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
struct Shell {
    std::vector<std::pair<glm::vec2, glm::vec2>> walls;
    float low = std::numeric_limits<float>::max(), high = -low;
};
std::string buildingName(const std::string& name, const char* suffix) {
    const std::string ending(suffix);
    if (name.rfind("building_", 0) || name.size() <= ending.size() ||
        name.compare(name.size() - ending.size(), ending.size(), ending)) return {};
    return name.substr(0, name.size() - ending.size());
}
std::vector<std::vector<glm::vec2>> rings(const Shell& shell) {
    std::vector<glm::vec2> vertices;
    const auto vertex = [&](glm::vec2 p) {
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (glm::distance(vertices[i], p) <= weld) return i;
        vertices.push_back(p); return vertices.size() - 1;
    };
    for (const auto& edge : shell.walls) { vertex(edge.first); vertex(edge.second); }
    std::set<std::pair<std::size_t, std::size_t>> edges;
    // Foundation bands/gables can split one wall edge into several spans.
    // Split overlapping spans at every welded vertex before tracing cycles.
    for (const auto& edge : shell.walls) {
        const auto delta = edge.second - edge.first;
        const float squared = glm::dot(delta, delta);
        if (squared < weld * weld) continue;
        std::vector<std::pair<float, std::size_t>> along;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const float t = glm::dot(vertices[i] - edge.first, delta) / squared;
            if (t < -.001f || t > 1.001f || glm::distance(vertices[i], edge.first + delta * t) > weld) continue;
            along.emplace_back(t, i);
        }
        std::sort(along.begin(), along.end());
        for (std::size_t i = 1; i < along.size(); ++i)
            if (along[i - 1].second != along[i].second)
                edges.emplace(std::minmax(along[i - 1].second, along[i].second));
    }
    std::vector<std::vector<std::size_t>> neighbors(vertices.size());
    for (const auto& edge : edges) {
        neighbors[edge.first].push_back(edge.second); neighbors[edge.second].push_back(edge.first);
    }
    for (const auto& adjacent : neighbors) if (adjacent.size() != 2) return {};
    std::vector<bool> visited(vertices.size());
    std::vector<std::vector<glm::vec2>> result;
    for (std::size_t start = 0; start < vertices.size(); ++start) {
        if (visited[start]) continue;
        std::vector<glm::vec2> polygon;
        auto current = start, previous = vertices.size();
        do {
            if (visited[current]) return {};
            visited[current] = true; polygon.push_back(vertices[current]);
            const auto& next = neighbors[current];
            const auto selected = next[0] == previous ? next[1] : next[0];
            previous = current; current = selected;
        } while (current != start);
        if (!house_polygon::simple(polygon)) return {};
        result.push_back(std::move(polygon));
    }
    return result;
}
}

BuildingFootprints::BuildingFootprints(const Model& model) {
    std::map<std::string, Shell> shells;
    for (const auto& draw : model.draws) {
        if (draw.detailDistance > 0 && !draw.lowDetail) continue;
        const auto wall = buildingName(draw.name, "_walls"), roof = buildingName(draw.name, "_roof");
        if (wall.empty() && roof.empty()) continue;
        auto& shell = shells[wall.empty() ? roof : wall];
        const auto& primitive = model.primitives.at(draw.primitive);
        for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
            glm::vec3 p[3];
            bool valid = true;
            for (int j = 0; j < 3; ++j) {
                p[j] = glm::vec3(draw.transform * glm::vec4(primitive.vertices.at(primitive.indices[i + j]).position, 1));
                if (!finite(p[j])) { valid = false; continue; }
                shell.low = std::min(shell.low, p[j].y); shell.high = std::max(shell.high, p[j].y);
            }
            if (wall.empty() || !valid) continue;
            const auto normal = glm::cross(p[1] - p[0], p[2] - p[0]);
            if (glm::length(normal) < 1e-6f || std::abs(normal.y) > .1f * glm::length(normal)) continue;
            float longest = 0; std::pair<glm::vec2, glm::vec2> edge;
            for (int j = 0; j < 3; ++j) {
                const glm::vec2 a(p[j].x, p[j].z), b(p[(j + 1) % 3].x, p[(j + 1) % 3].z);
                if (glm::distance(a, b) > longest) { longest = glm::distance(a, b); edge = {a, b}; }
            }
            if (longest > weld) shell.walls.push_back(edge);
        }
    }
    // Windows, roof details and other building parts can extend beyond the
    // plain wall/roof shell. Use all parts for the requested vertical range.
    for (const auto& draw : model.draws) {
        auto name = draw.name;
        auto found = shells.end();
        for (auto separator = name.find_last_of('_'); separator != std::string::npos; separator = name.find_last_of('_')) {
            name.resize(separator); found = shells.find(name);
            if (found != shells.end()) break;
        }
        if (found == shells.end()) continue;
        const auto& primitive = model.primitives.at(draw.primitive);
        for (const auto index : primitive.indices) {
            const auto p = glm::vec3(draw.transform * glm::vec4(primitive.vertices.at(index).position, 1));
            if (finite(p)) { found->second.low = std::min(found->second.low, p.y); found->second.high = std::max(found->second.high, p.y); }
        }
    }
    std::vector<VisibilityBounds> bounds;
    for (const auto& [id, shell] : shells) {
        auto boundary = rings(shell);
        if (boundary.empty() || !std::isfinite(shell.low) || !std::isfinite(shell.high)) continue;
        glm::vec3 low(std::numeric_limits<float>::max()), high(-low);
        float area = 0;
        for (const auto& ring : boundary) {
            area += house_polygon::area(ring);
            for (const auto p : ring) { low = glm::min(low, {p.x, shell.low - 10, p.y}); high = glm::max(high, {p.x, shell.high + 10, p.y}); }
        }
        byId_.emplace(id, buildings_.size());
        buildings_.push_back({id, std::move(boundary), shell.low - 10, shell.high + 10, area});
        bounds.push_back({low, high});
    }
    index_ = VisibilityIndex(bounds);
}

bool BuildingFootprints::Building::contains(glm::vec3 point) const {
    if (!finite(point) || point.y < low || point.y > high) return false;
    bool inside = false;
    for (const auto& ring : rings) if (house_polygon::contains(ring, {point.x, point.z})) inside = !inside;
    return inside;
}
std::optional<std::string> BuildingFootprints::find(glm::vec3 point) const {
    if (!finite(point)) return std::nullopt;
    std::vector<std::size_t> candidates;
    index_.query({point, point}, candidates);
    const Building* selected = nullptr;
    for (const auto index : candidates) {
        const auto& building = buildings_[index];
        if (building.contains(point) && (!selected || building.area < selected->area ||
            (building.area == selected->area && building.id < selected->id))) selected = &building;
    }
    return selected ? std::optional<std::string>(selected->id) : std::nullopt;
}
bool BuildingFootprints::contains(const std::string& building, glm::vec3 point) const {
    const auto found = byId_.find(building);
    return found != byId_.end() && buildings_[found->second].contains(point);
}

} // namespace viewer
