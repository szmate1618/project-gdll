#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
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
};

struct ItemModelConfig {
    float massKg = .3f; // One physical inventory stack's mass, in kilograms.
    std::vector<ItemModelPart> parts; // Empty preserves the generic item model.
};
} // namespace viewer
