#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace viewer {
enum class ItemModelShape { Box, Frustum };

// Parts use local Y-up metres. Size is full width/height/depth; a frustum's
// bottom footprint uses size.x/z, and topSize is its full top width/depth.
struct ItemModelPart {
    std::string name;
    ItemModelShape shape = ItemModelShape::Box;
    std::string material;
    glm::vec3 center{0};
    glm::vec3 size{1};
    glm::vec3 color{1};
    glm::vec2 topSize{1};
    glm::quat rotation{1,0,0,0}; // Part-local orientation in the item frame.
};

struct ItemModelVertex {
    glm::vec3 position{0}; // Model-local metres, sharing the primitive/grip frame.
    glm::vec3 normal{0,1,0};
    glm::vec3 color{1};
};

struct ItemModelConfig {
    float massKg = .3f; // One physical inventory stack's mass, in kilograms.
    std::vector<ItemModelPart> parts; // Empty preserves the generic item model.
    // Optional flat triangle list, at most 4095 vertices. Replaces primitive
    // visuals; parts still define the physical collision geometry.
    std::vector<ItemModelVertex> mesh;
};
} // namespace viewer
