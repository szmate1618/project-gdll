#pragma once

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string_view>

namespace viewer {
enum class PropColliderShape { Box, Sphere };
enum class PropMaterial : std::uint64_t { Unknown = 0, Wood = 1, Cardboard = 2 };

constexpr std::uint64_t propMaterialId(std::string_view material) {
    return material == "wood" ? static_cast<std::uint64_t>(PropMaterial::Wood) :
        material == "cardboard" ? static_cast<std::uint64_t>(PropMaterial::Cardboard) : 0;
}
constexpr bool nailableMaterial(std::uint64_t material) {
    return material == static_cast<std::uint64_t>(PropMaterial::Wood) ||
        material == static_cast<std::uint64_t>(PropMaterial::Cardboard);
}

// Collider centers/half extents are body-local metres, Y up. Spheres use the
// same radius in every half-extents component. Material IDs reach ray queries.
struct PropCollider {
    PropColliderShape shape = PropColliderShape::Box;
    glm::vec3 center{0};
    glm::vec3 halfExtents{.5f};
    std::uint64_t materialId = 0;
    glm::quat rotation{1,0,0,0};
};
} // namespace viewer
