#pragma once

#include "forest_layer.hpp"

namespace viewer {

struct ForestTreeVisibilityStats : VisibilityStats {
    std::size_t queriedPatches = 0;
};

// Immutable indexes over ordinary trees and each patch's generated/detected
// trees. Bounds include the complete transformed asset, including padding.
class ForestTreeVisibility {
public:
    ForestTreeVisibility() = default;
    ForestTreeVisibility(const std::vector<TreeInstance>& trees,
                         const std::vector<ForestPatch>& patches,
                         const std::vector<VisibilityBounds>& renderingBounds = {});
    // Returns original TreeLayer indices. Volume-only patches never enter a
    // tree index. A shadow caller supplies its own light projection/view.
    ForestTreeVisibilityStats query(const glm::mat4& projectionView,
        const std::vector<bool>& detailedPatches, bool culling,
        std::vector<std::size_t>& visible, const HorizontalVisibilityRange* range = nullptr) const;

private:
    struct Group {
        std::vector<std::size_t> trees;
        VisibilityBounds bounds;
        VisibilityIndex index;
    };
    // Group zero contains trees outside forests; subsequent groups are patches.
    std::vector<Group> groups_;
};

} // namespace viewer
