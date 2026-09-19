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
                          std::vector<std::size_t>& visible) const;

private:
    VisibilityIndex index_;
};

} // namespace viewer
