#include "visibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace viewer {

bool HorizontalVisibilityRange::intersects(const VisibilityBounds& bounds) const {
    const auto nearest = glm::clamp(center, glm::vec2(bounds.minimum.x, bounds.minimum.z),
                                   glm::vec2(bounds.maximum.x, bounds.maximum.z));
    const auto offset = nearest - center;
    return glm::dot(offset, offset) <= radius * radius;
}

bool HorizontalVisibilityRange::contains(const VisibilityBounds& bounds) const {
    const auto offset = glm::max(glm::abs(glm::vec2(bounds.minimum.x, bounds.minimum.z) - center),
                                 glm::abs(glm::vec2(bounds.maximum.x, bounds.maximum.z) - center));
    return glm::dot(offset, offset) <= radius * radius;
}

Frustum::Frustum(const glm::mat4& projectionView) {
    const auto rows = glm::transpose(projectionView);
    planes_ = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1], rows[3] - rows[1],
               rows[3] + rows[2], rows[3] - rows[2]};
    for (auto& plane : planes_) {
        const float length = glm::length(glm::vec3(plane));
        if (std::isfinite(length) && length > 0 && std::isfinite(plane.w)) plane /= length;
        else plane = glm::vec4(0, 0, 0, 1); // Degenerate planes cannot safely reject anything.
    }
}

Frustum::Relation Frustum::classify(const glm::vec3& minimum, const glm::vec3& maximum) const {
    std::size_t testedPlanes = 0, temporalRejects = 0;
    return classify<false>(minimum, maximum, nullptr, testedPlanes, temporalRejects);
}

template<bool Temporal>
Frustum::Relation Frustum::classify(const glm::vec3& minimum, const glm::vec3& maximum,
                                   std::uint8_t* lastRejectingPlane, std::size_t& testedPlanes,
                                   std::size_t& temporalRejects) const {
    const auto center = (minimum + maximum) * 0.5f;
    const auto halfSize = (maximum - minimum) * 0.5f;
    // Prevent float32 bounds from popping at clip planes; 1 cm is negligible at town scale.
    constexpr float tolerance = 0.01f;
    bool inside = true;
    // Swap the previous rejecting plane to the front. Every plane is still
    // evaluated at most once; accepted hints never skip the remaining planes.
    unsigned preferred = 0;
    if constexpr (Temporal) preferred = *lastRejectingPlane;
    for (unsigned test = 0; test < planes_.size(); ++test) {
        unsigned planeIndex = test;
        if constexpr (Temporal) planeIndex = test == 0 ? preferred : (test == preferred ? 0 : test);
        const auto& plane = planes_[planeIndex];
        const auto normal = glm::vec3(plane);
        const float distance = glm::dot(normal, center) + plane.w;
        const float radius = glm::dot(glm::abs(normal), halfSize);
        if (distance + radius < -tolerance) {
            testedPlanes += test + 1;
            if constexpr (Temporal) {
                if (test == 0) ++temporalRejects;
                *lastRejectingPlane = static_cast<std::uint8_t>(planeIndex);
            }
            return Relation::Outside;
        }
        if (distance - radius < tolerance) inside = false;
    }
    testedPlanes += planes_.size();
    return inside ? Relation::Inside : Relation::Intersecting;
}

VisibilityIndex::VisibilityIndex(const std::vector<VisibilityBounds>& bounds) : bounds_(bounds) {
    for (const auto& bound : bounds_) for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(bound.minimum[axis]) || !std::isfinite(bound.maximum[axis]) ||
            bound.minimum[axis] > bound.maximum[axis]) {
            throw std::runtime_error("Visibility requires finite, ordered bounds");
        }
    }
    order_.resize(bounds_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    if (!bounds_.empty()) build(0, bounds_.size());
}

std::size_t VisibilityIndex::build(std::size_t first, std::size_t count) {
    VisibilityBounds bounds{glm::vec3(std::numeric_limits<float>::max()),
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

template<bool PlaneHints>
void VisibilityIndex::visit(std::size_t index, const Frustum* frustum, const HorizontalVisibilityRange* range,
                            std::vector<std::size_t>& visible, VisibilityStats& stats,
                            VisibilityQueryState* temporal) const {
    const auto& node = nodes_[index];
    ++stats.testedNodes;
    if (range && !range->intersects(node.bounds)) return;
    std::uint8_t* nodePlane = nullptr;
    if constexpr (PlaneHints) nodePlane = &temporal->nodePlanes_[index];
    const auto relation = frustum ? frustum->classify<PlaneHints>(node.bounds.minimum, node.bounds.maximum,
        nodePlane, stats.testedPlanes, stats.temporalRejects) : Frustum::Relation::Inside;
    if (relation == Frustum::Relation::Outside) return;
    if (relation == Frustum::Relation::Inside && (!range || range->contains(node.bounds))) {
        visible.insert(visible.end(), order_.begin() + node.first, order_.begin() + node.first + node.count);
    } else if (node.left != 0) {
        visit<PlaneHints>(node.left, frustum, range, visible, stats, temporal);
        visit<PlaneHints>(node.right, frustum, range, visible, stats, temporal);
    } else for (std::size_t offset = node.first; offset < node.first + node.count; ++offset) {
        const auto instance = order_[offset];
        ++stats.testedInstances;
        if (range && !range->intersects(bounds_[instance])) continue;
        std::uint8_t* instancePlane = nullptr;
        if constexpr (PlaneHints) instancePlane = &temporal->instancePlanes_[instance];
        if (!frustum || frustum->classify<PlaneHints>(bounds_[instance].minimum, bounds_[instance].maximum,
                instancePlane, stats.testedPlanes, stats.temporalRejects) != Frustum::Relation::Outside)
            visible.push_back(instance);
    }
}

VisibilityStats VisibilityIndex::query(const glm::mat4& projectionView,
                                        std::vector<std::size_t>& visible,
                                        const HorizontalVisibilityRange* range, bool frustumCulling,
                                        VisibilityQueryState* temporal) const {
    visible.clear();
    return appendQuery(projectionView, visible, range, frustumCulling, temporal);
}

void VisibilityIndex::visit(std::size_t index, const VisibilityBounds& region,
                            std::vector<std::size_t>& visible, VisibilityStats& stats) const {
    const auto overlaps = [&region](const VisibilityBounds& bounds) {
        return glm::all(glm::lessThanEqual(bounds.minimum, region.maximum)) &&
               glm::all(glm::greaterThanEqual(bounds.maximum, region.minimum));
    };
    const auto& node = nodes_[index];
    ++stats.testedNodes;
    if (!overlaps(node.bounds)) return;
    if (node.left != 0) {
        visit(node.left, region, visible, stats);
        visit(node.right, region, visible, stats);
    } else for (std::size_t offset = node.first; offset < node.first + node.count; ++offset) {
        const auto instance = order_[offset];
        ++stats.testedInstances;
        if (overlaps(bounds_[instance])) visible.push_back(instance);
    }
}

VisibilityStats VisibilityIndex::query(const VisibilityBounds& region,
                                      std::vector<std::size_t>& visible) const {
    visible.clear();
    VisibilityStats stats;
    if (!nodes_.empty()) visit(0, region, visible, stats);
    return stats;
}

VisibilityStats VisibilityIndex::appendQuery(const glm::mat4& projectionView,
                                            std::vector<std::size_t>& visible,
                                            const HorizontalVisibilityRange* range, bool frustumCulling,
                                            VisibilityQueryState* temporal) const {
    VisibilityStats stats;
    if (nodes_.empty()) return stats;
    if (!frustumCulling) {
        visit<false>(0, nullptr, range, visible, stats, nullptr);
        return stats;
    }
    const Frustum frustum(projectionView);
    if (!temporal) {
        visit<false>(0, &frustum, range, visible, stats, nullptr);
        return stats;
    }
    bool coherent = true;
    if (temporal->initialized_) {
        // World-space planes are normalized, so offsets are in meters.
        // Cuts, teleports and large projection changes use cheaper fixed order.
        constexpr float minimumNormalDot = 0.98480775f; // cos(10 degrees)
        constexpr float maximumOffsetChange = 25.0f;
        for (std::size_t i = 0; i < frustum.planes_.size(); ++i) {
            const auto& current = frustum.planes_[i];
            const auto& previous = temporal->previousPlanes_[i];
            // Also handles unchanged degenerate planes (e.g. infinite far).
            if (current == previous) continue;
            if (glm::dot(glm::vec3(current), glm::vec3(previous)) < minimumNormalDot ||
                std::abs(current.w - previous.w) > maximumOffsetChange) {
                coherent = false;
                break;
            }
        }
    }
    temporal->previousPlanes_ = frustum.planes_;
    temporal->initialized_ = true;
    if (!coherent) {
        visit<false>(0, &frustum, range, visible, stats, nullptr);
        return stats;
    }
    temporal->nodePlanes_.resize(nodes_.size(), 0);
    temporal->instancePlanes_.resize(bounds_.size(), 0);
    visit<true>(0, &frustum, range, visible, stats, temporal);
    return stats;
}

} // namespace viewer
