#include "collision_box.hpp"
#include "collision_world.hpp"

#include <box3d/collision.h>
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
b3Vec3 vector(glm::vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vector(b3Vec3 p) { return {p.x, p.y, p.z}; }
b3Quat quaternion(glm::quat q) { return {{q.x, q.y, q.z}, q.w}; }

// All points in one manifold share a separating normal. Keep the deepest
// feature so the caller resolves its full penetration in a single translation.
std::optional<Contact> deepest(const b3LocalManifold& manifold, const CollisionBox& frame,
                               bool reverseNormal = false) {
    const b3LocalManifoldPoint* point = nullptr;
    for (int i = 0; i < manifold.pointCount; ++i)
        if (manifold.points[i].separation < 0 &&
            (!point || manifold.points[i].separation < point->separation)) point = &manifold.points[i];
    if (!point) return std::nullopt;
    const auto normal = frame.rotation * vector(manifold.normal) * (reverseNormal ? -1.f : 1.f);
    return Contact{normal, frame.center + frame.rotation * vector(point->point), -point->separation};
}
} // namespace

bool validCollisionBox(const CollisionBox& box) {
    const auto q = box.rotation;
    return finite(box.center) && finite(box.halfExtents) &&
        glm::all(glm::greaterThan(box.halfExtents, glm::vec3(0))) &&
        std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
        std::abs(glm::dot(q, q) - 1.f) < 1e-3f;
}

std::array<glm::vec3, 8> collisionBoxCorners(const CollisionBox& box) {
    std::array<glm::vec3, 8> corners;
    for (unsigned i = 0; i < corners.size(); ++i)
        corners[i] = box.center + box.rotation * (box.halfExtents *
            glm::vec3(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1));
    return corners;
}

std::pair<glm::vec3, glm::vec3> collisionBoxBounds(const CollisionBox& box, float skin) {
    const auto axes = glm::mat3_cast(box.rotation);
    const auto half = box.halfExtents + glm::vec3(skin);
    const auto extent = glm::abs(axes[0]) * half.x + glm::abs(axes[1]) * half.y + glm::abs(axes[2]) * half.z;
    return {box.center - extent, box.center + extent};
}

std::optional<Contact> collisionBoxTriangleContact(const CollisionBox& box,
    const std::array<glm::vec3, 3>& triangle, float skin) {
    if (!validCollisionBox(box) || !std::isfinite(skin) || skin < 0) return std::nullopt;
    const auto inverse = glm::conjugate(box.rotation);
    std::array<glm::vec3, 3> local;
    for (std::size_t i = 0; i < local.size(); ++i) {
        if (!finite(triangle[i])) return std::nullopt;
        local[i] = inverse * (triangle[i] - box.center);
    }
    const auto normal = glm::cross(local[1] - local[0], local[2] - local[0]);
    if (glm::dot(normal, normal) < 1e-16f) return std::nullopt;
    // Box3D's triangle narrowphase is one sided. Orient the face toward the
    // box center to preserve the collision world's double-sided surface rule.
    if (glm::dot(-local[0], normal) < 0) std::swap(local[1], local[2]);
    const auto half = box.halfExtents + glm::vec3(skin);
    const auto hull = b3MakeBoxHull(half.x, half.y, half.z);
    std::array<b3LocalManifoldPoint, 8> points{};
    b3LocalManifold manifold{}; manifold.points = points.data();
    b3SATCache cache{};
    b3CollideTriangleAndHull(&manifold, static_cast<int>(points.size()), vector(local[0]), vector(local[1]),
        vector(local[2]), 0xFF, &hull.base, &cache, false);
    return deepest(manifold, box);
}

std::optional<Contact> collisionBoxContact(const CollisionBox& box,
    const CollisionBox& obstacle, float skin) {
    if (!validCollisionBox(box) || !validCollisionBox(obstacle) || !std::isfinite(skin) || skin < 0)
        return std::nullopt;
    const auto half = box.halfExtents + glm::vec3(skin);
    const auto query = b3MakeBoxHull(half.x, half.y, half.z);
    const auto h = obstacle.halfExtents;
    const auto hull = b3MakeBoxHull(h.x, h.y, h.z);
    const auto inverse = glm::conjugate(obstacle.rotation);
    const b3Transform relative{vector(inverse * (box.center - obstacle.center)), quaternion(inverse * box.rotation)};
    std::array<b3LocalManifoldPoint, 8> points{};
    b3LocalManifold manifold{}; manifold.points = points.data();
    b3SATCache cache{};
    b3CollideHulls(&manifold, static_cast<int>(points.size()), &hull.base, &query.base, relative, &cache);
    return deepest(manifold, obstacle);
}

std::optional<Contact> collisionBoxCapsuleContact(const CollisionBox& box,
    glm::vec3 a, glm::vec3 b, float radius, float skin) {
    if (!validCollisionBox(box) || !finite(a) || !finite(b) || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(skin) || skin < 0) return std::nullopt;
    const auto inverse = glm::conjugate(box.rotation);
    const b3Capsule capsule{vector(inverse * (a - box.center)), vector(inverse * (b - box.center)), radius};
    const auto half = box.halfExtents + glm::vec3(skin);
    const auto hull = b3MakeBoxHull(half.x, half.y, half.z);
    std::array<b3LocalManifoldPoint, 8> points{};
    b3LocalManifold manifold{}; manifold.points = points.data();
    b3SimplexCache cache{};
    b3CollideHullAndCapsule(&manifold, static_cast<int>(points.size()), &hull.base, &capsule, b3Transform_identity, &cache);
    return deepest(manifold, box, true);
}
} // namespace viewer
