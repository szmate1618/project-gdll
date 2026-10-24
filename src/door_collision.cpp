#include "zombie_update_profile.hpp"
#include "door_collision.hpp"

#include <box3d/collision.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace viewer {
namespace {
using GroupKey = std::pair<std::string, std::array<float, 16>>;
GroupKey groupKey(const DrawInstance& draw) {
    std::array<float, 16> matrix{};
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) matrix[c * 4 + r] = draw.transform[c][r];
    return {draw.detailGroup, matrix};
}
bool suffix(const std::string& name, const std::string& ending) {
    return name.size() >= ending.size() && name.compare(name.size() - ending.size(), ending.size(), ending) == 0;
}
using Polygon = std::vector<glm::vec3>;

// Partition a convex polygon at a doorway plane. Signed distance >= 0 is
// inside the cutter. Outside pieces are retained as masonry; the remainder is
// partitioned at the next plane. Interpolation retains the source wall plane.
std::pair<Polygon, Polygon> partition(const Polygon& polygon, glm::vec3 axis, glm::vec3 origin, float offset) {
    Polygon inside, outside;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const double da = glm::dot(glm::dvec3(a) - glm::dvec3(origin), glm::dvec3(axis)) - offset;
        const double db = glm::dot(glm::dvec3(b) - glm::dvec3(origin), glm::dvec3(axis)) - offset;
        (da >= 0 ? inside : outside).push_back(a);
        if ((da < 0) != (db < 0)) {
            const auto point = glm::vec3(glm::dvec3(a) + (glm::dvec3(b) - glm::dvec3(a)) * (da / (da - db)));
            inside.push_back(point); outside.push_back(point);
        }
    }
    return {std::move(inside), std::move(outside)};
}
void triangulate(const Polygon& p, std::vector<CollisionFace>& output) {
    for (std::size_t i = 1; i + 1 < p.size(); ++i) {
        if (glm::length(glm::cross(glm::dvec3(p[i]) - glm::dvec3(p[0]), glm::dvec3(p[i + 1]) - glm::dvec3(p[0]))) > 1e-10)
            output.push_back({p[0], p[i], p[i + 1]});
    }
}
b3Vec3 vector(glm::vec3 v) { return {v.x, v.y, v.z}; }
glm::vec3 vector(b3Vec3 v) { return {v.x, v.y, v.z}; }
} // namespace

std::vector<std::vector<DoorOpening>> collisionDoorOpenings(const Model& model, const DoorLayer& doors) {
    std::map<GroupKey, std::vector<DoorOpening>> groups;
    // Requiring the matching detailed masonry avoids making invisible holes in
    // older maps where door rectangles were drawn on top of uncut walls.
    for (const auto& draw : model.draws)
        if (!draw.lowDetail && !draw.detailGroup.empty() && suffix(draw.name, "_detailed_walls"))
            groups.emplace(groupKey(draw), std::vector<DoorOpening>{});
    for (std::size_t i = 0; i < doors.count(); ++i) {
        const auto& panel = doors.panel(i);
        const auto& draw = model.draws.at(panel.draw);
        const auto found = groups.find(groupKey(draw));
        if (found == groups.end()) continue;
        float bottom = std::numeric_limits<float>::max(), top = -bottom;
        for (const auto& v : model.primitives.at(draw.primitive).vertices) {
            const float y = (draw.transform * glm::vec4(v.position, 1)).y;
            bottom = std::min(bottom, y); top = std::max(top, y);
        }
        found->second.push_back({panel.hinge, glm::cross(glm::vec3(0, 1, 0), panel.outward),
                                 panel.outward, 2 * panel.halfExtents.x, bottom - .001f, top});
    }
    std::vector<std::vector<DoorOpening>> result(model.draws.size());
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        if (!draw.lowDetail || draw.detailGroup.empty()) continue;
        const auto found = groups.find(groupKey(draw));
        if (found != groups.end()) result[i] = found->second;
    }
    return result;
}

std::vector<CollisionFace> cutCollisionDoorways(const CollisionFace& face, const std::vector<DoorOpening>& openings) {
    std::vector<CollisionFace> faces{face};
    for (const auto& door : openings) {
        std::vector<CollisionFace> cut;
        for (const auto& triangle : faces) {
            bool intersects = true;
            float low = std::numeric_limits<float>::max(), high = -low, bottom = low, top = high;
            for (const auto p : triangle) {
                if (std::abs(glm::dot(p - door.hinge, door.outward)) > .02f) intersects = false;
                const float x = glm::dot(p - door.hinge, door.right);
                low = std::min(low, x); high = std::max(high, x);
                bottom = std::min(bottom, p.y); top = std::max(top, p.y);
            }
            const auto normal = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
            if (!intersects || high <= 0 || low >= door.width || bottom >= door.top || top <= door.bottom ||
                std::abs(glm::dot(normal, door.outward)) < .99f * glm::length(normal)) {
                cut.push_back(triangle); continue;
            }
            Polygon remaining(triangle.begin(), triangle.end());
            const std::array<glm::vec3, 4> axes{{door.right, -door.right, {0, 1, 0}, {0, -1, 0}}};
            const std::array<float, 4> offsets{{0, -door.width, door.bottom - door.hinge.y, door.hinge.y - door.top}};
            for (std::size_t plane = 0; plane < axes.size() && !remaining.empty(); ++plane) {
                auto parts = partition(remaining, axes[plane], door.hinge, offsets[plane]);
                triangulate(parts.second, cut);
                remaining = std::move(parts.first);
            }
        }
        faces = std::move(cut);
    }
    return faces;
}

void appendDoorContacts(const DoorLayer& doors, glm::vec3 feet, float radius, float height,
                        float skin, std::vector<Contact>& contacts) {
    const float reach = radius + skin;
    const auto minimum = feet - glm::vec3(reach, skin, reach);
    const auto maximum = feet + glm::vec3(reach, height + skin, reach);
    const auto candidates=doors.colliders(minimum,maximum);
    if (zombie_profile::activeProfile) zombie_profile::activeProfile->doorCandidates+=candidates.size();
    for (const auto& door:candidates) {
        const auto inverse = glm::conjugate(door.rotation);
        b3Capsule capsule{vector(inverse * (feet + glm::vec3(0, radius, 0) - door.center)),
                          vector(inverse * (feet + glm::vec3(0, height - radius, 0) - door.center)), reach};
        const auto h = door.halfExtents;
        const auto box = b3MakeBoxHull(h.x, h.y, h.z);
        std::array<b3LocalManifoldPoint, 8> points{};
        b3LocalManifold manifold{};
        manifold.points = points.data();
        b3SimplexCache cache{};
        b3CollideHullAndCapsule(&manifold, static_cast<int>(points.size()), &box.base, &capsule, b3Transform_identity, &cache);
        const auto normal = door.rotation * vector(manifold.normal);
        for (int i = 0; i < manifold.pointCount; ++i)
            if (points[i].separation < 0) {
                if (zombie_profile::activeProfile) ++zombie_profile::activeProfile->doorOverlaps;
                contacts.push_back({normal, door.center + door.rotation * vector(points[i].point), -points[i].separation});
            }
    }
}

bool doorsBlockSight(const DoorLayer& doors, glm::vec3 origin, glm::vec3 target) {
    for (const auto& door : doors.colliders(glm::min(origin, target), glm::max(origin, target))) {
        const auto inverse = glm::conjugate(door.rotation);
        const auto start = inverse * (origin - door.center), end = inverse * (target - door.center);
        if (glm::all(glm::lessThanEqual(glm::abs(start), door.halfExtents)) ||
            glm::all(glm::lessThanEqual(glm::abs(end), door.halfExtents))) return true;
        const auto h = door.halfExtents;
        const auto box = b3MakeBoxHull(h.x, h.y, h.z);
        b3RayCastInput ray{};
        ray.origin = vector(start); ray.translation = vector(end - start); ray.maxFraction = 1;
        if (b3RayCastHull(&box.base, &ray).hit) return true;
    }
    return false;
}

std::vector<glm::vec3> doorCollisionDebugLines(const DoorLayer& doors, glm::vec3 minimum, glm::vec3 maximum) {
    std::vector<glm::vec3> result;
    for (const auto& door : doors.colliders(minimum, maximum)) {
        std::array<glm::vec3, 8> corners;
        for (unsigned i = 0; i < 8; ++i)
            corners[i] = door.center + door.rotation * (door.halfExtents * glm::vec3(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1));
        for (unsigned i = 0; i < 8; ++i) for (unsigned bit : {1u, 2u, 4u})
            if (!(i & bit)) result.insert(result.end(), {corners[i], corners[i | bit]});
    }
    return result;
}
} // namespace viewer
