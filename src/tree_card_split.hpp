#pragma once
#include "model.hpp"
#include <optional>

namespace viewer {
// CPU-only preparation of upright healthy-tree cards. Original vertices and
// indices stay first, followed by two replacement quads in the same primitive.
// UVs remain in the original image; no texture or instance bounds are changed.
struct TreeCardSplit {
    Primitive mesh;
    float canopyV = 0;
    glm::vec2 trunkU{0, 1};
    float areaRatio = 1;
};
// Requires an upright rectangular card with an affine, full-image UV mapping.
// The caller restricts this to healthy tree assets, excluding shrubs/bare trees.
std::optional<TreeCardSplit> splitTreeCard(const Model& model, const Primitive& card);
} // namespace viewer
