#pragma once

#include "tree_layer.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace viewer {

class TreeFrustum {
public:
    enum class Relation { Outside, Intersecting, Inside };
    explicit TreeFrustum(const glm::mat4& projectionView);
    Relation classify(const glm::vec3& minimum, const glm::vec3& maximum) const;
    bool intersects(const glm::vec3& minimum, const glm::vec3& maximum) const {
        return classify(minimum, maximum) != Relation::Outside;
    }

private:
    std::array<glm::vec4, 6> planes_{};
};

struct TreeVisibilityStats {
    std::size_t testedNodes = 0;
    std::size_t testedInstances = 0;
};

// Immutable median-split BVH over full padded instance bounds. Queries return
// original instance indices, conservatively retaining intersecting bounds.
class TreeVisibility {
public:
    TreeVisibility() = default;
    explicit TreeVisibility(const std::vector<TreeInstance>& instances);
    TreeVisibilityStats query(const glm::mat4& projectionView,
                              std::vector<std::size_t>& visible) const;

private:
    struct Bounds { glm::vec3 minimum, maximum; };
    struct Node {
        Bounds bounds;
        std::size_t first = 0, count = 0, left = 0, right = 0;
    };
    std::size_t build(std::size_t first, std::size_t count);
    void visit(std::size_t node, const TreeFrustum& frustum,
               std::vector<std::size_t>& visible, TreeVisibilityStats& stats) const;
    std::vector<Bounds> bounds_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
};

} // namespace viewer
