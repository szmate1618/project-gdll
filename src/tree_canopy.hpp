#pragma once

#include "tree_layer.hpp"

namespace viewer {

// Daytime brightness at 20% and 95% of visible tree height. Runtime Y is up;
// baseY is the authoritative instance's foot height, not padded geometry bounds.
struct TreeCanopyShade {
    float baseY = 0;
    float inverseHeight = 1;
    float lowerLight = 1;
    float upperLight = 1;
};
static_assert(sizeof(TreeCanopyShade) == 4 * sizeof(float), "Canopy attribute must contain four packed floats");

// Static, camera-independent approximation of neighboring canopy cover. Uses
// visible crown dimensions, excluding each tree's own cards; no asset mutation.
// Builds a temporary spatial index once, with no per-frame neighbor queries.
std::vector<TreeCanopyShade> estimateTreeCanopyShade(const std::vector<TreeInstance>& trees);

} // namespace viewer
