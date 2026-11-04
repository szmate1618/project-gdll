#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <optional>
#include <utility>

namespace viewer {
struct Contact;

// Oriented solid box in world meters, Y up. Rotation must be a unit quaternion.
struct CollisionBox {
    glm::vec3 center{0};
    glm::vec3 halfExtents{.30f, .225f, .50f};
    glm::quat rotation{1, 0, 0, 0};
};

bool validCollisionBox(const CollisionBox& box);
std::array<glm::vec3, 8> collisionBoxCorners(const CollisionBox& box);
// Skin expands each local half extent before deriving the conservative AABB.
std::pair<glm::vec3, glm::vec3> collisionBoxBounds(const CollisionBox& box, float skin = 0);

// Contact normals point from the obstacle toward the querying box. Triangle
// faces are double sided, including thin walls and downward-wound floors.
std::optional<Contact> collisionBoxTriangleContact(const CollisionBox& box,
    const std::array<glm::vec3, 3>& triangle, float skin = 0);
std::optional<Contact> collisionBoxContact(const CollisionBox& box,
    const CollisionBox& obstacle, float skin = 0);
// Capsule endpoints denote the centers of its two rounded caps, in world meters.
std::optional<Contact> collisionBoxCapsuleContact(const CollisionBox& box,
    glm::vec3 a, glm::vec3 b, float radius, float skin = 0);
} // namespace viewer
