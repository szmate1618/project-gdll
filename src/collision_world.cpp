#include "collision_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>

namespace viewer {
namespace {

constexpr float epsilon = 1e-5f;

struct Bounds {
    glm::vec3 low{std::numeric_limits<float>::max()};
    glm::vec3 high{std::numeric_limits<float>::lowest()};
    void include(glm::vec3 p) { low = glm::min(low, p); high = glm::max(high, p); }
    void include(const Bounds& other) { include(other.low); include(other.high); }
    bool intersects(const Bounds& other) const {
        return low.x <= other.high.x && high.x >= other.low.x &&
               low.y <= other.high.y && high.y >= other.low.y &&
               low.z <= other.high.z && high.z >= other.low.z;
    }
};

struct Triangle {
    glm::vec3 a, b, c, normal;
    Bounds bounds;
    int group = -1;
    bool terrain = false;
    bool roof = false;
};

float squareLength(glm::vec3 p) { return glm::dot(p, p); }

bool finite(glm::vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The barycentric test is evaluated in double precision to keep projection
// tests reliable for the small roof triangles in a kilometer-scale scene.
bool verticalIntersection(const Triangle& t, float x, float z, float& y) {
    const double ax = t.a.x, az = t.a.z;
    const double bx = static_cast<double>(t.b.x) - ax;
    const double bz = static_cast<double>(t.b.z) - az;
    const double cx = static_cast<double>(t.c.x) - ax;
    const double cz = static_cast<double>(t.c.z) - az;
    const double px = static_cast<double>(x) - ax;
    const double pz = static_cast<double>(z) - az;
    const double determinant = bx * cz - bz * cx;
    if (std::abs(determinant) < 1e-12) return false;
    const double u = (px * cz - pz * cx) / determinant;
    const double v = (bx * pz - bz * px) / determinant;
    if (u < -1e-6 || v < -1e-6 || u + v > 1.000001) return false;
    y = static_cast<float>(t.a.y + u * (t.b.y - t.a.y) + v * (t.c.y - t.a.y));
    return true;
}

glm::vec3 closestTrianglePoint(glm::vec3 p, const Triangle& t) {
    const glm::dvec3 a(t.a), b(t.b), c(t.c), point(p);
    const auto ab = b - a, ac = c - a, ap = point - a;
    const double d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return t.a;
    const auto bp = point - b;
    const double d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return t.b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return glm::vec3(a + (d1 / (d1 - d3)) * ab);
    const auto cp = point - c;
    const double d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return t.c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return glm::vec3(a + (d2 / (d2 - d6)) * ac);
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        return glm::vec3(b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b));
    }
    const double inverse = 1.0 / (va + vb + vc);
    return glm::vec3(a + ab * (vb * inverse) + ac * (vc * inverse));
}

struct ClosestPair { glm::vec3 segment, triangle; };

ClosestPair closestSegments(glm::vec3 p, glm::vec3 q, glm::vec3 a, glm::vec3 b) {
    const auto d1 = q - p, d2 = b - a, r = p - a;
    const float aa = glm::dot(d1, d1), ee = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);
    float s = 0, t = 0;
    if (aa <= 1e-12f && ee <= 1e-12f) return {p, a};
    if (aa <= 1e-12f) t = glm::clamp(f / ee, 0.0f, 1.0f);
    else {
        const float c = glm::dot(d1, r);
        if (ee <= 1e-12f) s = glm::clamp(-c / aa, 0.0f, 1.0f);
        else {
            const float bb = glm::dot(d1, d2), denominator = aa * ee - bb * bb;
            if (denominator > 1e-12f) s = glm::clamp((bb * f - c * ee) / denominator, 0.0f, 1.0f);
            t = (bb * s + f) / ee;
            if (t < 0) { t = 0; s = glm::clamp(-c / aa, 0.0f, 1.0f); }
            else if (t > 1) { t = 1; s = glm::clamp((bb - c) / aa, 0.0f, 1.0f); }
        }
    }
    return {p + d1 * s, a + d2 * t};
}

ClosestPair closestSegmentTriangle(glm::vec3 p, glm::vec3 q, const Triangle& t) {
    const float dp = glm::dot(p - t.a, t.normal);
    const float dq = glm::dot(q - t.a, t.normal);
    if (dp * dq <= 0 && std::abs(dp - dq) > 1e-9f) {
        const auto hit = p + (q - p) * (dp / (dp - dq));
        const auto onTriangle = closestTrianglePoint(hit, t);
        if (squareLength(hit - onTriangle) < 1e-10f) return {hit, hit};
    }
    ClosestPair best{p, closestTrianglePoint(p, t)};
    float distanceSquared = squareLength(best.segment - best.triangle);
    const auto consider = [&](ClosestPair pair) {
        const float candidate = squareLength(pair.segment - pair.triangle);
        if (candidate < distanceSquared) { best = pair; distanceSquared = candidate; }
    };
    consider({q, closestTrianglePoint(q, t)});
    consider(closestSegments(p, q, t.a, t.b));
    consider(closestSegments(p, q, t.b, t.c));
    consider(closestSegments(p, q, t.c, t.a));
    return best;
}

std::string buildingName(std::string name) {
    if (name.rfind("building_", 0) != 0) return {};
    for (const std::string suffix : {"_walls", "_roof"}) {
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.resize(name.size() - suffix.size());
            break;
        }
    }
    return name;
}

void appendBox(std::vector<glm::vec3>& lines, const Bounds& b) {
    std::array<glm::vec3, 8> corners;
    for (unsigned i = 0; i < 8; ++i) {
        corners[i] = {i & 1 ? b.high.x : b.low.x, i & 2 ? b.high.y : b.low.y,
                      i & 4 ? b.high.z : b.low.z};
    }
    for (unsigned i = 0; i < 8; ++i) for (unsigned bit : {1u, 2u, 4u}) {
        if (!(i & bit)) { lines.push_back(corners[i]); lines.push_back(corners[i | bit]); }
    }
}

}  // namespace

struct CollisionWorld::Impl {
    struct Node {
        Bounds bounds;
        std::uint32_t first = 0, count = 0, left = 0, right = 0;
    };
    struct Group { Bounds bounds; bool namedBuilding = false; };
    std::vector<Triangle> triangles;
    std::vector<std::uint32_t> order;
    std::vector<Node> nodes;
    std::vector<Group> groups;
    Bounds bounds;
    CollisionStats stats;
    bool hasTerrain = false;

    std::uint32_t build(std::uint32_t begin, std::uint32_t end) {
        const auto index = static_cast<std::uint32_t>(nodes.size());
        nodes.emplace_back();
        Bounds region, centers;
        for (auto i = begin; i < end; ++i) {
            const auto& t = triangles[order[i]];
            region.include(t.bounds);
            centers.include((t.bounds.low + t.bounds.high) * 0.5f);
        }
        nodes[index].bounds = region;
        if (end - begin <= 8) {
            nodes[index].first = begin;
            nodes[index].count = end - begin;
            return index;
        }
        const auto extent = centers.high - centers.low;
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > extent[axis]) axis = 2;
        const auto middle = begin + (end - begin) / 2;
        std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
            [&](std::uint32_t a, std::uint32_t b) {
                return triangles[a].bounds.low[axis] + triangles[a].bounds.high[axis] <
                       triangles[b].bounds.low[axis] + triangles[b].bounds.high[axis];
            });
        const auto left = build(begin, middle);
        const auto right = build(middle, end);
        nodes[index].left = left;
        nodes[index].right = right;
        return index;
    }

    template <class Callback> void query(const Bounds& region, Callback callback) const {
        if (nodes.empty()) return;
        // Median splitting bounds depth to fewer than 32 for uint32 indices.
        std::array<std::uint32_t, 64> stack{};
        std::size_t count = 1;
        while (count) {
            const auto& node = nodes[stack[--count]];
            if (!node.bounds.intersects(region)) continue;
            if (node.count) {
                for (auto i = node.first; i < node.first + node.count; ++i) {
                    const auto& t = triangles[order[i]];
                    if (t.bounds.intersects(region)) callback(t);
                }
            } else {
                stack[count++] = node.left;
                stack[count++] = node.right;
            }
        }
    }
};

CollisionWorld::CollisionWorld(const Model& model) : impl_(std::make_unique<Impl>()) {
    auto& data = *impl_;
    std::unordered_map<std::string, int> buildingGroups;
    for (const auto& draw : model.draws) {
        if (draw.primitive >= model.primitives.size()) continue;
        const auto& primitive = model.primitives[draw.primitive];
        // Mirrored nodes reverse the index winding, while the renderer flips
        // its front-face convention. Preserve the same physical surface side.
        const float winding = glm::determinant(glm::mat3(draw.transform)) < 0 ? -1.0f : 1.0f;
        const auto namedBuilding = buildingName(draw.name);
        const bool terrain = draw.name.rfind("terrain", 0) == 0 || draw.name.rfind("road", 0) == 0;
        const bool roof = !namedBuilding.empty() && draw.name.find("_roof") != std::string::npos;
        int group = -1;
        if (!terrain) {
            if (!namedBuilding.empty()) {
                const auto inserted = buildingGroups.emplace(namedBuilding, static_cast<int>(data.groups.size()));
                group = inserted.first->second;
                if (inserted.second) data.groups.push_back({{}, true});
            } else {
                group = static_cast<int>(data.groups.size());
                data.groups.push_back({{}, false});
            }
        }
        for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
            const auto ia = primitive.indices[i], ib = primitive.indices[i + 1], ic = primitive.indices[i + 2];
            if (ia >= primitive.vertices.size() || ib >= primitive.vertices.size() || ic >= primitive.vertices.size()) continue;
            Triangle triangle;
            triangle.a = glm::vec3(draw.transform * glm::vec4(primitive.vertices[ia].position, 1));
            triangle.b = glm::vec3(draw.transform * glm::vec4(primitive.vertices[ib].position, 1));
            triangle.c = glm::vec3(draw.transform * glm::vec4(primitive.vertices[ic].position, 1));
            const auto cross = glm::cross(glm::dvec3(triangle.b) - glm::dvec3(triangle.a),
                                           glm::dvec3(triangle.c) - glm::dvec3(triangle.a));
            const double length = glm::length(cross);
            if (!finite(triangle.a) || !finite(triangle.b) || !finite(triangle.c) || !std::isfinite(length) || length < 1e-10) {
                ++data.stats.skippedDegenerateTriangles;
                continue;
            }
            triangle.normal = glm::vec3(cross / length) * winding;
            triangle.bounds.include(triangle.a); triangle.bounds.include(triangle.b); triangle.bounds.include(triangle.c);
            triangle.group = group;
            triangle.terrain = terrain;
            triangle.roof = roof;
            data.bounds.include(triangle.bounds);
            if (group >= 0) data.groups[static_cast<std::size_t>(group)].bounds.include(triangle.bounds);
            data.triangles.push_back(triangle);
            data.stats.terrainTriangles += terrain;
            data.stats.buildingTriangles += !namedBuilding.empty();
        }
    }
    data.stats.triangles = data.triangles.size();
    data.stats.buildings = buildingGroups.size();
    data.hasTerrain = data.stats.terrainTriangles > 0;
    data.order.resize(data.triangles.size());
    std::iota(data.order.begin(), data.order.end(), 0u);
    data.nodes.reserve(data.triangles.size() / 3 + 1);
    if (!data.order.empty()) data.build(0, static_cast<std::uint32_t>(data.order.size()));
    data.stats.bvhNodes = data.nodes.size();
}

CollisionWorld::~CollisionWorld() = default;
CollisionWorld::CollisionWorld(CollisionWorld&&) noexcept = default;
CollisionWorld& CollisionWorld::operator=(CollisionWorld&&) noexcept = default;

std::vector<Contact> CollisionWorld::contacts(glm::vec3 feet, float radius, float height, float skin) const {
    std::vector<Contact> result;
    if (!finite(feet) || radius <= 0 || height < radius * 2 || skin < 0) return result;
    const float reach = radius + skin;
    const glm::vec3 p = feet + glm::vec3(0, radius, 0), q = feet + glm::vec3(0, height - radius, 0);
    Bounds region; region.include(p - glm::vec3(reach)); region.include(q + glm::vec3(reach));
    impl_->query(region, [&](const Triangle& t) {
        const auto pair = closestSegmentTriangle(p, q, t);
        const auto separation = pair.segment - pair.triangle;
        const float squared = squareLength(separation);
        if (squared >= reach * reach) return;
        const float distance = std::sqrt(std::max(squared, 0.0f));
        auto normal = t.normal;
        if (distance > 1e-6f) normal = separation / distance;
        else if (glm::dot((p + q) * 0.5f - t.a, normal) < 0) normal = -normal;
        result.push_back({normal, pair.triangle, reach - distance});
    });
    std::sort(result.begin(), result.end(), [](const Contact& a, const Contact& b) { return a.depth > b.depth; });
    return result;
}

std::optional<GroundHit> CollisionWorld::groundAt(float x, float z, float minY, float maxY, bool terrainOnly) const {
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(minY) || !std::isfinite(maxY) || minY > maxY) return std::nullopt;
    Bounds region; region.include({x - epsilon, minY, z - epsilon}); region.include({x + epsilon, maxY, z + epsilon});
    std::optional<GroundHit> result;
    impl_->query(region, [&](const Triangle& t) {
        if (terrainOnly && !t.terrain) return;
        auto normal = t.normal;
        if (t.terrain && normal.y < 0) normal = -normal;
        if (normal.y <= 1e-4f) return;
        float y;
        if (!verticalIntersection(t, x, z, y) || y < minY - epsilon || y > maxY + epsilon) return;
        if (!result || y > result->height) result = GroundHit{y, normal};
    });
    return result;
}

bool CollisionWorld::insideBuilding(glm::vec3 feet, float radius, float height) const {
    if (impl_->triangles.empty() || !finite(feet) || radius <= 0 || height < radius * 2) return false;
    Bounds column;
    column.include({feet.x - epsilon, impl_->bounds.low.y - 1, feet.z - epsilon});
    column.include({feet.x + epsilon, impl_->bounds.high.y + 1, feet.z + epsilon});
    std::map<int, std::vector<float>> crossings;
    bool insideNamed = false;
    impl_->query(column, [&](const Triangle& t) {
        if (t.group < 0) return;
        const auto& group = impl_->groups[static_cast<std::size_t>(t.group)];
        if (feet.y >= group.bounds.high.y - epsilon || feet.y + height <= group.bounds.low.y + epsilon) return;
        float y;
        if (!verticalIntersection(t, feet.x, feet.z, y)) return;
        // Generated roofs triangulate the actual footprint, including holes;
        // their projection therefore preserves concavities and courtyards.
        if (group.namedBuilding && t.roof && feet.y + radius < y - epsilon &&
            feet.y + height - radius > group.bounds.low.y + epsilon) insideNamed = true;
        crossings[t.group].push_back(y);
    });
    if (insideNamed) return true;
    for (auto& item : crossings) {
        auto& values = item.second;
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end(), [](float a, float b) { return std::abs(a - b) < 1e-4f; }), values.end());
        // Closed generic meshes use even/odd vertical crossings. Open meshes
        // remain surfaces; they are never expanded into arbitrary solid boxes.
        if (values.size() % 2 != 0) continue;
        for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
            if (feet.y + radius < values[i + 1] - epsilon && feet.y + height - radius > values[i] + epsilon) return true;
        }
    }
    return false;
}

std::optional<glm::vec3> CollisionWorld::findSpawn(glm::vec3 preferredEye, float eyeHeight,
    float radius, float height, float maxSlopeDegrees) const {
    if (impl_->triangles.empty() || !finite(preferredEye) || radius <= 0 || height < radius * 2) return std::nullopt;
    const float slopeCosine = std::cos(glm::radians(glm::clamp(maxSlopeDegrees, 0.0f, 89.0f)));
    const float minimumY = impl_->bounds.low.y - height - 1;
    const float maximumY = impl_->bounds.high.y + height + 1;
    const auto attempt = [&](float x, float z, float ceiling, bool terrainOnly) -> std::optional<glm::vec3> {
        const auto ground = groundAt(x, z, minimumY, ceiling, terrainOnly);
        if (!ground || ground->normal.y < slopeCosine) return std::nullopt;
        glm::vec3 feet(x, ground->height + radius * (1 / ground->normal.y - 1) + 0.01f, z);
        if (insideBuilding(feet, radius, height)) return std::nullopt;
        for (const auto& contact : contacts(feet, radius, height)) if (contact.depth > 0.005f) return std::nullopt;
        return feet;
    };
    const auto nearby = [&](glm::vec3 center, float ceiling, bool terrainOnly) -> std::optional<glm::vec3> {
        if (const auto hit = attempt(center.x, center.z, ceiling, terrainOnly)) return hit;
        for (float distance : {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f}) {
            for (int direction = 0; direction < 24; ++direction) {
                const float angle = direction * (6.28318530718f / 24);
                if (const auto hit = attempt(center.x + distance * std::cos(angle), center.z + distance * std::sin(angle), ceiling, terrainOnly)) return hit;
            }
        }
        return std::nullopt;
    };
    const float preferredCeiling = std::min(maximumY, std::max(minimumY, preferredEye.y - eyeHeight + 0.4f));
    if (const auto hit = nearby(preferredEye, preferredCeiling, impl_->hasTerrain)) return hit;
    // A camera below the surface or far outside the scene still gets a valid
    // ground spawn; the scene center is only a fallback after the local search.
    if (const auto hit = nearby(preferredEye, maximumY, impl_->hasTerrain)) return hit;
    const auto center = (impl_->bounds.low + impl_->bounds.high) * 0.5f;
    if (const auto hit = nearby(center, maximumY, impl_->hasTerrain)) return hit;
    for (int z = 1; z < 16; ++z) for (int x = 1; x < 16; ++x) {
        const auto p = glm::mix(impl_->bounds.low, impl_->bounds.high, glm::vec3(x / 16.0f, 0, z / 16.0f));
        if (const auto hit = attempt(p.x, p.z, maximumY, impl_->hasTerrain)) return hit;
    }
    if (impl_->hasTerrain) return nearby(center, maximumY, false);
    return std::nullopt;
}

std::vector<glm::vec3> CollisionWorld::debugLines(glm::vec3 near, float distance) const {
    std::vector<glm::vec3> result;
    if (!finite(near) || distance <= 0) return result;
    Bounds region; region.include(near - glm::vec3(distance)); region.include(near + glm::vec3(distance));
    std::vector<int> groups;
    impl_->query(region, [&](const Triangle& t) {
        if (result.size() < 30000) result.insert(result.end(), {t.a, t.b, t.b, t.c, t.c, t.a});
        if (t.group >= 0 && impl_->groups[static_cast<std::size_t>(t.group)].namedBuilding) groups.push_back(t.group);
    });
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    for (const auto group : groups) appendBox(result, impl_->groups[static_cast<std::size_t>(group)].bounds);
    return result;
}

const CollisionStats& CollisionWorld::stats() const { return impl_->stats; }

}  // namespace viewer
