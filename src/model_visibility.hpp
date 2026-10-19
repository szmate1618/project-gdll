#pragma once

#include "model.hpp"
#include "visibility.hpp"

namespace viewer {

// Caches full transformed source-draw bounds at construction. Each draw
// transform is applied to its primitive bounds exactly once; query results are
// original Model::draws indices for direct GPU-draw lookup.
class ModelVisibility {
public:
    ModelVisibility() = default;
    explicit ModelVisibility(const Model& model);
    VisibilityStats query(const glm::mat4& projectionView,
                          std::vector<std::size_t>& visible,
                          const HorizontalVisibilityRange* range = nullptr,
                          bool frustumCulling = true) const;
    // Filter existing BVH results by nearest transformed bounds, accounting for
    // perspective magnification. Also used when frustum culling is disabled.
    void filterDetails(const glm::vec3& eye, const glm::mat4& projection,
                       std::vector<std::size_t>& visible) const;

private:
    VisibilityIndex index_;
    std::vector<VisibilityBounds> drawBounds_;
    std::vector<VisibilityBounds> detailBounds_;
    std::vector<float> detailDistances_;
    std::vector<bool> lowDetails_;
};

} // namespace viewer
