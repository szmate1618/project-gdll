#pragma once

#include "tree_layer.hpp"
#include "visibility.hpp"

#include <vector>

namespace viewer {

using TreeFrustum = Frustum;
using TreeVisibilityStats = VisibilityStats;

// Immutable median-split BVH over full padded instance bounds. Queries return
// original instance indices, conservatively retaining intersecting bounds.
class TreeVisibility {
public:
    TreeVisibility() = default;
    explicit TreeVisibility(const std::vector<TreeInstance>& instances);
    TreeVisibilityStats query(const glm::mat4& projectionView,
                              std::vector<std::size_t>& visible) const;

private:
    VisibilityIndex index_;
};

} // namespace viewer
