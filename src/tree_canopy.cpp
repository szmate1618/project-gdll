#include "tree_canopy.hpp"
#include "visibility.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
float canopyRadius(const TreeInstance& tree) {
    // Equal-area circular proxy for the visible crown footprint. Limit very
    // large crowns to a local 60 m influence rather than shading an entire map.
    return std::min(20.0f, 0.5f * std::sqrt(tree.dimensions.x) * std::sqrt(tree.dimensions.z));
}

float cover(const TreeInstance& neighbor, float radius, glm::vec3 sample) {
    const auto delta = glm::vec2(neighbor.transform[3].x - sample.x,
                                neighbor.transform[3].z - sample.z);
    const float distance = glm::length(delta);
    const float above = neighbor.transform[3].y + neighbor.dimensions.y - sample.y;
    // Broad, soft coverage approximates obstructed sky, including oblique rays.
    // Crowns below the sample cannot darken it. No billboard normal is used.
    const float edge = 1.0f - glm::smoothstep(radius, 3.0f * radius, distance);
    const float overhead = glm::smoothstep(0.0f, 0.35f * neighbor.dimensions.y, above);
    // Barely overtopping crowns obstruct little sky at the exposed tree top.
    return edge * overhead * overhead * radius * radius / (radius * radius + glm::dot(delta, delta));
}

float light(double coverage) {
    // Retain detail even in dense interiors; exposed/isolated trees stay at 1.
    return static_cast<float>(0.30 + 0.70 * std::exp(-1.5 * coverage));
}
} // namespace

std::vector<TreeCanopyShade> estimateTreeCanopyShade(const std::vector<TreeInstance>& trees) {
    std::vector<VisibilityBounds> influence;
    std::vector<float> radii;
    influence.reserve(trees.size());
    radii.reserve(trees.size());
    for (const auto& tree : trees) {
        const float radius = canopyRadius(tree);
        radii.push_back(radius);
        const glm::vec3 center(tree.transform[3].x, 0, tree.transform[3].z);
        const glm::vec3 extent(3.0f * radius, 0, 3.0f * radius);
        influence.push_back({center - extent, center + extent});
    }
    const VisibilityIndex index(influence);
    std::vector<std::size_t> neighbors;
    std::vector<TreeCanopyShade> result;
    result.reserve(trees.size());
    for (std::size_t i = 0; i < trees.size(); ++i) {
        const auto& tree = trees[i];
        const glm::vec3 feet(tree.transform[3]);
        const glm::vec3 point(feet.x, 0, feet.z);
        index.query(VisibilityBounds{point, point}, neighbors);
        double lowerCover = 0, upperCover = 0;
        for (const auto neighbor : neighbors) {
            if (neighbor == i) continue;
            lowerCover += cover(trees[neighbor], radii[neighbor], feet + glm::vec3(0, 0.20f * tree.dimensions.y, 0));
            upperCover += cover(trees[neighbor], radii[neighbor], feet + glm::vec3(0, 0.95f * tree.dimensions.y, 0));
        }
        result.push_back({feet.y, 1.0f / tree.dimensions.y, light(lowerCover), light(upperCover)});
    }
    return result;
}
} // namespace viewer
