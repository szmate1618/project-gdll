#pragma once

#include "tree_layer.hpp"
#include "visibility.hpp"
#include <optional>

namespace viewer {

// Recognizes the existing two-upright/two-horizontal quad assets. Other assets
// retain their authored geometry. Indices refer to Model::draws, not primitives.
struct TreeBillboardLayout {
    std::size_t sideDraw = 0, topDraw = 0;
    glm::vec3 topPivot{0}; // Mesh-local center of the mid-height canopy card.
};
std::optional<TreeBillboardLayout> treeBillboardLayout(const Model& model);
enum class TreeBillboardMode { Full, Side, Top };
// Three-dimensional distance to the root. Absolute elevation >45 degrees uses
// the top card, including views from below (an intentional texture approximation).
TreeBillboardMode treeBillboardMode(glm::vec3 root, glm::vec3 eye, float distance);
// Contains authored geometry, every root-centered side yaw, and every rotation
// of the top card about its center, preserving node/instance transforms once.
VisibilityBounds treeBillboardBounds(const TreeInstance& instance, const Model& model,
                                    const TreeBillboardLayout& layout);

} // namespace viewer
