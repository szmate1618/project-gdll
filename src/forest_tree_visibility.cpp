#include "forest_tree_visibility.hpp"

#include <limits>
#include <stdexcept>

namespace viewer {

ForestTreeVisibility::ForestTreeVisibility(const std::vector<TreeInstance>& trees,
                                         const std::vector<ForestPatch>& patches) {
    groups_.resize(patches.size() + 1);
    std::vector<bool> assigned(trees.size(), false);
    const auto assign = [&](std::size_t tree, Group& group) {
        if (tree >= trees.size() || assigned[tree])
            throw std::runtime_error("Invalid or overlapping forest tree index");
        assigned[tree] = true;
        group.trees.push_back(tree);
    };
    for (std::size_t i = 0; i < patches.size(); ++i) {
        const auto& patch = patches[i];
        if (patch.firstTree > trees.size() || patch.treeCount > trees.size() - patch.firstTree)
            throw std::runtime_error("Forest tree range exceeds the tree layer");
        auto& group = groups_[i + 1];
        group.trees.reserve(patch.treeCount + patch.detectedTrees.size());
        for (std::size_t tree = patch.firstTree; tree < patch.firstTree + patch.treeCount; ++tree)
            assign(tree, group);
        for (const auto tree : patch.detectedTrees) assign(tree, group);
    }
    for (std::size_t tree = 0; tree < trees.size(); ++tree)
        if (!assigned[tree]) groups_[0].trees.push_back(tree);

    for (auto& group : groups_) {
        std::vector<VisibilityBounds> bounds;
        bounds.reserve(group.trees.size());
        group.bounds = {glm::vec3(std::numeric_limits<float>::max()),
                        glm::vec3(std::numeric_limits<float>::lowest())};
        for (const auto tree : group.trees) {
            bounds.push_back({trees[tree].boundsMin, trees[tree].boundsMax});
            group.bounds.minimum = glm::min(group.bounds.minimum, trees[tree].boundsMin);
            group.bounds.maximum = glm::max(group.bounds.maximum, trees[tree].boundsMax);
        }
        group.index = VisibilityIndex(bounds);
    }
}

ForestTreeVisibilityStats ForestTreeVisibility::query(const glm::mat4& projectionView,
    const std::vector<bool>& detailedPatches, bool culling,
    std::vector<std::size_t>& visible) const {
    visible.clear();
    ForestTreeVisibilityStats stats;
    if (groups_.empty()) return stats;
    if (detailedPatches.size() + 1 != groups_.size())
        throw std::runtime_error("Forest detail selection does not match visibility groups");
    const Frustum frustum(projectionView);
    for (std::size_t i = 0; i < groups_.size(); ++i) {
        if (i && !detailedPatches[i - 1]) continue;
        const auto& group = groups_[i];
        if (group.trees.empty()) continue;
        if (culling && !frustum.intersects(group.bounds.minimum, group.bounds.maximum)) continue;
        if (i) ++stats.queriedPatches;
        if (!culling) {
            visible.insert(visible.end(), group.trees.begin(), group.trees.end());
            continue;
        }
        const auto first = visible.size();
        const auto tested = group.index.appendQuery(projectionView, visible);
        stats.testedNodes += tested.testedNodes;
        stats.testedInstances += tested.testedInstances;
        for (std::size_t tree = first; tree < visible.size(); ++tree)
            visible[tree] = group.trees[visible[tree]];
    }
    return stats;
}

} // namespace viewer
