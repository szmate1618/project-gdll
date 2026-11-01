#include "house_polygon.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace viewer::house_polygon {
namespace {
float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }
float signedArea(const Polygon& polygon) {
    if (polygon.empty()) return 0;
    // Translate before summing to retain precision at large map coordinates.
    double sum = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i] - polygon[0], b = polygon[(i + 1) % polygon.size()] - polygon[0];
        sum += double(a.x) * b.y - double(a.y) * b.x;
    }
    return static_cast<float>(sum * .5);
}
float distanceSquared(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const auto e = b - a;
    const auto d = p - a - e * glm::clamp(glm::dot(p - a, e) / glm::dot(e, e), 0.f, 1.f);
    return glm::dot(d, d);
}
void simplify(Polygon& polygon) {
    bool changed = true;
    while (changed && polygon.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto a = polygon[(i + polygon.size() - 1) % polygon.size()], b = polygon[i], c = polygon[(i + 1) % polygon.size()];
            // Projection and float32 export can leave sub-millimetre spurs
            // when a clip lies on an exterior facade. Remove them before a
            // world transform can collapse them into overlapping edges.
            const float length = std::max(glm::distance(a, b), glm::distance(b, c));
            if (glm::distance(a, b) < .0005f || std::abs(cross(b - a, c - b)) < .0005f * length) {
                polygon.erase(polygon.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; break;
            }
        }
    }
}
} // namespace
float area(const Polygon& polygon) { return std::abs(signedArea(polygon)); }
bool contains(const Polygon& polygon, glm::vec2 p, float margin) {
    bool inside = false;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        if (glm::dot(b - a, b - a) > 0 && distanceSquared(p, a, b) <= margin * margin) return true;
        if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (b.x - a.x) * (p.y - a.y) / (b.y - a.y)) inside = !inside;
    }
    return inside;
}
bool simple(const Polygon& polygon) {
    // Small clipping tiles are still real geometry. Room/building minimum areas
    // are enforced by their callers; discarding these tiles punches floor holes.
    if (polygon.size() < 3 || area(polygon) < .00000001f) return false;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        if (glm::distance(a, b) < .0001f) return false;
        for (std::size_t j = i + 1; j < polygon.size(); ++j) {
            if (j == i + 1 || (i == 0 && j + 1 == polygon.size())) continue;
            const auto c = polygon[j], d = polygon[(j + 1) % polygon.size()];
            const float determinant = cross(b - a, d - c);
            if (std::abs(determinant) > .000001f) {
                const auto t = cross(c - a, d - c) / determinant, u = cross(c - a, b - a) / determinant;
                if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return false;
            } else if (distanceSquared(a, c, d) < .00000001f || distanceSquared(b, c, d) < .00000001f ||
                       distanceSquared(c, a, b) < .00000001f || distanceSquared(d, a, b) < .00000001f) return false;
        }
    }
    return true;
}
Polygon clip(const Polygon& polygon, glm::vec2 low, glm::vec2 high) {
    if (glm::any(glm::lessThanEqual(high, low))) return {};
    Polygon result = polygon;
    for (int axis = 0; axis < 2; ++axis) for (const bool upper : {false, true}) {
        const float cut = upper ? high[axis] : low[axis];
        Polygon next;
        const auto inside = [&](glm::vec2 p) { return upper ? p[axis] <= cut : p[axis] >= cut; };
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto a = result[i], b = result[(i + 1) % result.size()];
            if (inside(a)) next.push_back(a);
            if (inside(a) != inside(b)) {
                auto p = a + (b - a) * ((cut - a[axis]) / (b[axis] - a[axis]));
                p[axis] = cut; next.push_back(p);
            }
        }
        simplify(next); result = std::move(next);
    }
    return simple(result) ? result : Polygon{};
}
bool covers(const Polygon& polygon, glm::vec2 low, glm::vec2 high) {
    const float expected = (high.x - low.x) * (high.y - low.y);
    return expected > 0 && area(clip(polygon, low, high)) >= expected - .0001f;
}
std::vector<std::uint32_t> triangles(const Polygon& polygon) {
    std::vector<std::uint32_t> remaining(polygon.size()), result;
    std::iota(remaining.begin(), remaining.end(), 0);
    const float winding = signedArea(polygon) < 0 ? -1.f : 1.f;
    while (remaining.size() > 2) {
        bool removed = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i], c = remaining[(i + 1) % remaining.size()];
            if (cross(polygon[b] - polygon[a], polygon[c] - polygon[b]) * winding <= .000001f) continue;
            bool occupied = false;
            for (const auto id : remaining) if (id != a && id != b && id != c) {
                const auto p = polygon[id];
                if (cross(polygon[b] - polygon[a], p - polygon[a]) * winding >= -.000001f &&
                    cross(polygon[c] - polygon[b], p - polygon[b]) * winding >= -.000001f &&
                    cross(polygon[a] - polygon[c], p - polygon[c]) * winding >= -.000001f) occupied = true;
            }
            if (occupied) continue;
            // Clockwise X/Z triangles face upward in the runtime X/Y/Z frame.
            result.insert(result.end(), winding < 0 ? std::initializer_list<std::uint32_t>{a, b, c} :
                                                     std::initializer_list<std::uint32_t>{a, c, b});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i)); removed = true; break;
        }
        if (!removed) return {};
    }
    return result;
}
std::vector<std::pair<float, float>> spans(const Polygon& polygon, int axis, float position, float low, float high) {
    const int along = 1 - axis;
    std::vector<float> cuts{low, high};
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        if ((a[axis] > position) == (b[axis] > position)) continue;
        const float p = a[along] + (b[along] - a[along]) * ((position - a[axis]) / (b[axis] - a[axis]));
        if (p > low && p < high) cuts.push_back(p);
    }
    std::sort(cuts.begin(), cuts.end());
    std::vector<std::pair<float, float>> result;
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        if (cuts[i] - cuts[i - 1] < .0001f) continue;
        glm::vec2 p; p[axis] = position; p[along] = (cuts[i - 1] + cuts[i]) * .5f;
        if (contains(polygon, p)) {
            if (!result.empty() && std::abs(result.back().second - cuts[i - 1]) < .0001f) result.back().second = cuts[i];
            else result.emplace_back(cuts[i - 1], cuts[i]);
        }
    }
    return result;
}
} // namespace viewer::house_polygon
