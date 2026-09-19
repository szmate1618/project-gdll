#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace viewer {

// Axis-aligned world-space meters, with Y up.
struct VisibilityBounds {
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
};

class Frustum {
public:
    enum class Relation { Outside, Intersecting, Inside };
    // Extracts OpenGL clip planes from a projection * view matrix.
    explicit Frustum(const glm::mat4& projectionView);
    Relation classify(const glm::vec3& minimum, const glm::vec3& maximum) const;
    bool intersects(const glm::vec3& minimum, const glm::vec3& maximum) const {
        return classify(minimum, maximum) != Relation::Outside;
    }

private:
    std::array<glm::vec4, 6> planes_{};
};

struct VisibilityStats {
    std::size_t testedNodes = 0;
    std::size_t testedInstances = 0;
};

// Immutable median-split BVH. Query clears visible and returns original bounds
// indices, retaining intersecting bounds conservatively.
class VisibilityIndex {
public:
    VisibilityIndex() = default;
    explicit VisibilityIndex(const std::vector<VisibilityBounds>& bounds);
    VisibilityStats query(const glm::mat4& projectionView,
                          std::vector<std::size_t>& visible) const;

private:
    struct Node {
        VisibilityBounds bounds;
        std::size_t first = 0, count = 0, left = 0, right = 0;
    };
    std::size_t build(std::size_t first, std::size_t count);
    void visit(std::size_t index, const Frustum& frustum,
               std::vector<std::size_t>& visible, VisibilityStats& stats) const;
    std::vector<VisibilityBounds> bounds_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
};

} // namespace viewer
