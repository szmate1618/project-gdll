#include "tree_visibility.hpp"

#include <vector>

namespace viewer {

TreeVisibility::TreeVisibility(const std::vector<TreeInstance>& instances) {
    std::vector<VisibilityBounds> bounds;
    bounds.reserve(instances.size());
    for (const auto& instance : instances) bounds.push_back({instance.boundsMin, instance.boundsMax});
    index_ = VisibilityIndex(bounds);
}

TreeVisibilityStats TreeVisibility::query(const glm::mat4& projectionView,
                                         std::vector<std::size_t>& visible) const {
    return index_.query(projectionView, visible);
}

} // namespace viewer
