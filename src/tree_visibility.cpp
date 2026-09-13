#include "tree_visibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace viewer {

TreeFrustum::TreeFrustum(const glm::mat4& projectionView) {
    const auto rows = glm::transpose(projectionView);
    planes_ = {rows[3] + rows[0], rows[3] - rows[0],
               rows[3] + rows[1], rows[3] - rows[1],
               rows[3] + rows[2], rows[3] - rows[2]};
    for (auto& plane : planes_) {
        const float length = glm::length(glm::vec3(plane));
        if (std::isfinite(length) && length > 0 && std::isfinite(plane.w)) plane /= length;
        else plane = glm::vec4(0, 0, 0, 1); // Degenerate planes cannot safely reject anything.
    }
}

TreeFrustum::Relation TreeFrustum::classify(const glm::vec3& minimum,
                                          const glm::vec3& maximum) const {
    const auto center = (minimum + maximum) * 0.5f;
    const auto halfSize = (maximum - minimum) * 0.5f;
    // Favor drawing marginal bounds rather than popping at a clip plane due to
    // matrix/bounds float32 rounding. One centimeter is negligible at town scale.
    constexpr float tolerance = 0.01f;
    bool inside = true;
    for (const auto& plane : planes_) {
        const auto normal = glm::vec3(plane);
        const float distance = glm::dot(normal, center) + plane.w;
        const float radius = glm::dot(glm::abs(normal), halfSize);
        if (distance + radius < -tolerance) return Relation::Outside;
        if (distance - radius < tolerance) inside = false;
    }
    return inside ? Relation::Inside : Relation::Intersecting;
}

TreeVisibility::TreeVisibility(const std::vector<TreeInstance>& instances) {
    bounds_.reserve(instances.size());
    for (const auto& instance : instances) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(instance.boundsMin[axis]) || !std::isfinite(instance.boundsMax[axis]) ||
                instance.boundsMin[axis] > instance.boundsMax[axis])
                throw std::runtime_error("Tree visibility requires finite, ordered instance bounds");
        }
        bounds_.push_back({instance.boundsMin, instance.boundsMax});
    }
    order_.resize(instances.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    if (!instances.empty()) build(0, instances.size());
}

std::size_t TreeVisibility::build(std::size_t first, std::size_t count) {
    Bounds bounds{glm::vec3(std::numeric_limits<float>::max()),
                  glm::vec3(std::numeric_limits<float>::lowest())};
    for (std::size_t offset = first; offset < first + count; ++offset) {
        bounds.minimum = glm::min(bounds.minimum, bounds_[order_[offset]].minimum);
        bounds.maximum = glm::max(bounds.maximum, bounds_[order_[offset]].maximum);
    }
    const auto index = nodes_.size();
    nodes_.push_back({bounds, first, count, 0, 0});
    if (count <= 32) return index;
    const auto extent = bounds.maximum - bounds.minimum;
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > extent[axis]) axis = 2;
    const auto middle = first + count / 2;
    std::nth_element(order_.begin() + first, order_.begin() + middle, order_.begin() + first + count,
                     [this, axis](std::size_t a, std::size_t b) {
        const float ca = bounds_[a].minimum[axis] + bounds_[a].maximum[axis];
        const float cb = bounds_[b].minimum[axis] + bounds_[b].maximum[axis];
        return ca != cb ? ca < cb : a < b;
    });
    const auto left = build(first, middle - first);
    const auto right = build(middle, first + count - middle);
    nodes_[index].left = left;
    nodes_[index].right = right;
    return index;
}

void TreeVisibility::visit(std::size_t index, const TreeFrustum& frustum,
                           std::vector<std::size_t>& visible, TreeVisibilityStats& stats) const {
    const auto& node = nodes_[index];
    ++stats.testedNodes;
    const auto relation = frustum.classify(node.bounds.minimum, node.bounds.maximum);
    if (relation == TreeFrustum::Relation::Outside) return;
    if (relation == TreeFrustum::Relation::Inside) {
        visible.insert(visible.end(), order_.begin() + node.first, order_.begin() + node.first + node.count);
    } else if (node.left != 0) {
        visit(node.left, frustum, visible, stats);
        visit(node.right, frustum, visible, stats);
    } else {
        for (std::size_t offset = node.first; offset < node.first + node.count; ++offset) {
            const auto instance = order_[offset];
            ++stats.testedInstances;
            if (frustum.intersects(bounds_[instance].minimum, bounds_[instance].maximum)) visible.push_back(instance);
        }
    }
}

TreeVisibilityStats TreeVisibility::query(const glm::mat4& projectionView,
                                         std::vector<std::size_t>& visible) const {
    visible.clear();
    TreeVisibilityStats stats;
    if (!nodes_.empty()) visit(0, TreeFrustum(projectionView), visible, stats);
    return stats;
}

} // namespace viewer
