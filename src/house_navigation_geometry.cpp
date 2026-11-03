#include "house_navigation_geometry.hpp"
#include "house_polygon.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace viewer {
namespace {
constexpr float clearance = .35f; // Upright body radius plus the walking wall margin.
float pointDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const auto edge = b - a;
    const float squared = glm::dot(edge, edge);
    const auto delta = p - a - edge * (squared > 0 ? glm::clamp(glm::dot(p - a, edge) / squared, 0.f, 1.f) : 0.f);
    return glm::dot(delta, delta);
}
float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }
float segmentDistance(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
    const auto ab = b - a, cd = d - c;
    const float divisor = cross(ab, cd);
    if (std::abs(divisor) > 1e-8f) {
        const float t = cross(c - a, cd) / divisor, u = cross(c - a, ab) / divisor;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0;
    }
    return std::min({pointDistance(a, c, d), pointDistance(b, c, d), pointDistance(c, a, b), pointDistance(d, a, b)});
}
void cornerCandidates(const std::vector<glm::vec2>& polygon, std::vector<glm::vec2>& result) {
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto point = polygon[i];
        const auto a = glm::normalize(polygon[(i + 1) % polygon.size()] - point);
        const auto b = glm::normalize(polygon[(i + polygon.size() - 1) % polygon.size()] - point);
        for (const auto axis : {a + b, a - b}) {
            const float length = glm::length(axis);
            if (length < 1e-5f) continue;
            for (const float sign : {-1.f, 1.f}) result.push_back(point + axis / length * (.55f * sign));
        }
    }
}
}

bool HouseNavigationRoom::walkable(glm::vec2 point) const {
    if (!house_polygon::contains(polygon, point)) return false;
    const auto nearEdge = [&](const auto& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i)
            if (pointDistance(point, ring[i], ring[(i + 1) % ring.size()]) < clearance * clearance) return true;
        return false;
    };
    if (nearEdge(polygon)) return false;
    for (const auto& ring : blocked) if (house_polygon::contains(ring, point) || nearEdge(ring)) return false;
    return true;
}
bool HouseNavigationRoom::clearSegment(glm::vec2 from, glm::vec2 to) const {
    if (!walkable(from) || !walkable(to)) return false;
    const auto nearEdge = [&](const auto& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i)
            if (segmentDistance(from, to, ring[i], ring[(i + 1) % ring.size()]) < clearance * clearance) return true;
        return false;
    };
    if (nearEdge(polygon)) return false;
    for (const auto& ring : blocked) if (nearEdge(ring)) return false;
    return true;
}
std::optional<glm::vec3> HouseNavigationRoom::center(const std::vector<glm::vec2>& anchors, const CollisionWorld& world) const {
    glm::vec2 ideal(0), low(std::numeric_limits<float>::max()), high(-low);
    for (const auto p : polygon) { ideal += p / float(polygon.size()); low = glm::min(low, p); high = glm::max(high, p); }
    std::vector<glm::vec2> candidates{ideal};
    candidates.insert(candidates.end(), anchors.begin(), anchors.end());
    for (int x = 1; x < 17; ++x) for (int z = 1; z < 17; ++z)
        candidates.push_back(glm::mix(low, high, glm::vec2(x / 17.f, z / 17.f)));
    cornerCandidates(polygon, candidates);
    for (const auto& ring : blocked) cornerCandidates(ring, candidates);
    std::stable_sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
        return glm::dot(a - ideal, a - ideal) < glm::dot(b - ideal, b - ideal);
    });
    for (const auto p : candidates) {
        if (!walkable(p)) continue;
        const auto ground = world.staticGroundAt(p.x, p.y, floor - .05f, floor + .05f);
        if (ground && ground->normal.y > .99f) return glm::vec3(p.x, ground->height + .004f, p.y);
    }
    return std::nullopt;
}
std::vector<glm::vec3> HouseNavigationRoom::path(glm::vec2 from, glm::vec2 to) const {
    const auto world = [&](glm::vec2 p) { return glm::vec3(p.x, floor + .004f, p.y); };
    if (clearSegment(from, to)) return {world(from), world(to)};
    if (!walkable(from) || !walkable(to)) return {};
    std::vector<glm::vec2> corners;
    cornerCandidates(polygon, corners);
    for (const auto& ring : blocked) cornerCandidates(ring, corners);
    std::vector<glm::vec2> points{from, to};
    for (const auto p : corners) if (walkable(p)) points.push_back(p);
    const auto none = points.size();
    std::vector<float> distance(points.size(), std::numeric_limits<float>::infinity());
    std::vector<std::size_t> previous(points.size(), none);
    using Entry = std::pair<float, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pending;
    distance[0] = 0; pending.emplace(0, 0);
    while (!pending.empty()) {
        const auto [cost, node] = pending.top(); pending.pop();
        if (cost != distance[node]) continue;
        if (node == 1) break;
        for (std::size_t next = 0; next < points.size(); ++next) {
            if (next == node || !clearSegment(points[node], points[next])) continue;
            const float candidate = cost + glm::distance(points[node], points[next]);
            if (candidate < distance[next]) {
                distance[next] = candidate; previous[next] = node; pending.emplace(candidate, next);
            }
        }
    }
    if (!std::isfinite(distance[1])) return {};
    std::vector<glm::vec3> result;
    for (std::size_t node = 1; node != none; node = previous[node]) result.push_back(world(points[node]));
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace viewer
