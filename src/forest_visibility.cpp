#include "forest_visibility.hpp"

namespace viewer {

OldForestVisibility::OldForestVisibility(const std::vector<ForestPatch>& patches) {
    std::vector<VisibilityBounds> bounds;
    for (const auto& patch : patches) {
        if (patch.oldForest.exterior.empty()) continue;
        auto box = patch.bounds;
        glm::dvec2 lo = patch.oldForest.exterior.front(), hi = lo;
        for (const auto p : patch.oldForest.exterior) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
        box.minimum.x = static_cast<float>(lo.x);
        box.maximum.x = static_cast<float>(hi.x);
        box.minimum.z = static_cast<float>(-hi.y);
        box.maximum.z = static_cast<float>(-lo.y);
        if (glm::any(glm::greaterThan(box.minimum, box.maximum))) continue;
        bounds.push_back(box);
        areas_.push_back(patch.oldForest);
    }
    index_ = VisibilityIndex(bounds);
}

std::optional<HorizontalVisibilityRange> OldForestVisibility::rangeAt(glm::vec3 camera) const {
    std::vector<std::size_t> candidates;
    index_.query(VisibilityBounds{camera, camera}, candidates);
    const glm::dvec2 point(camera.x, -camera.z);
    for (const auto i : candidates)
        if (forestContains(areas_[i], point))
            return HorizontalVisibilityRange{{camera.x, camera.z}, distance};
    return std::nullopt;
}

} // namespace viewer
