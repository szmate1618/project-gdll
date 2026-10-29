#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace viewer {

// Axis-aligned world-space meters, with Y up.
struct VisibilityBounds {
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
};

// World X/Z meters, unlimited vertically. Bounds tests retain intersecting
// geometry, including crowns rooted outside the circle.
struct HorizontalVisibilityRange {
    glm::vec2 center{0};
    float radius = 50;
    bool intersects(const VisibilityBounds& bounds) const;
    bool contains(const VisibilityBounds& bounds) const;
};

class Frustum {
public:
    enum class Relation { Outside, Intersecting, Inside };
    // Extracts OpenGL clip planes from a projection * view matrix.
    explicit Frustum(const glm::mat4& projectionView);
    Relation classify(const glm::vec3& minimum, const glm::vec3& maximum) const;
    bool intersects(const glm::vec3& minimum, const glm::vec3& maximum) const;

private:
    friend class VisibilityIndex;
    template<bool Temporal>
    Relation classify(const glm::vec3& minimum, const glm::vec3& maximum,
                      std::uint8_t* lastRejectingPlane, std::size_t& testedPlanes,
                      std::size_t& temporalRejects) const;
    std::array<glm::vec4, 6> planes_{};
    std::array<glm::vec3, 6> absoluteNormals_{};
};

struct VisibilityStats {
    std::size_t testedNodes = 0;
    std::size_t testedInstances = 0;
    std::size_t testedPlanes = 0;
    // First-plane rejects on the hinted path, including its initial plane 0.
    std::size_t temporalRejects = 0;
};

// Caller-owned plane-order hints, one per view/pass (or concurrent query).
// Every visibility decision still uses the current frustum and range.
class VisibilityQueryState {
private:
    friend class VisibilityIndex;
    std::vector<std::uint8_t> nodePlanes_;
    std::vector<std::uint8_t> instancePlanes_;
    std::array<glm::vec4, 6> previousPlanes_{};
    bool initialized_ = false;
};

// Median-split BVH with optional leaf refits. Query clears visible and returns
// original bounds indices, retaining intersecting bounds conservatively.
class VisibilityIndex {
public:
    VisibilityIndex() = default;
    explicit VisibilityIndex(const std::vector<VisibilityBounds>& bounds);
    // Optional horizontal range is enforced even when frustum culling is off.
    // History tries the previous rejecting plane first; large view/projection
    // changes use fixed order for the current query. Disabled frustum culling
    // leaves history untouched.
    VisibilityStats query(const glm::mat4& projectionView,
                          std::vector<std::size_t>& visible,
                          const HorizontalVisibilityRange* range = nullptr,
                          bool frustumCulling = true,
                          VisibilityQueryState* temporal = nullptr) const;
    // Refit a moved bound without changing IDs/topology. Returns false for an
    // unchanged bound. Reconstruct the index when its population changes.
    bool update(std::size_t instance, const VisibilityBounds& bounds);
    // Intersect both a frustum and a world-space region during BVH traversal.
    VisibilityStats query(const glm::mat4& projectionView, const VisibilityBounds& region,
                          std::vector<std::size_t>& visible) const;
    // Exact AABB overlap, including touching boundaries and point queries.
    VisibilityStats query(const VisibilityBounds& region,
                          std::vector<std::size_t>& visible) const;
    // Append without allocating a temporary result, for queries over disjoint
    // indexes. Appended indices still refer to this index's source bounds.
    VisibilityStats appendQuery(const glm::mat4& projectionView,
                                std::vector<std::size_t>& visible,
                                const HorizontalVisibilityRange* range = nullptr,
                                bool frustumCulling = true,
                                VisibilityQueryState* temporal = nullptr) const;

private:
    struct Node {
        VisibilityBounds bounds;
        std::size_t first = 0, count = 0, left = 0, right = 0, parent = 0;
    };
    std::size_t build(std::size_t first, std::size_t count);
    template<bool PlaneHints>
    void visit(std::size_t index, const Frustum* frustum, const HorizontalVisibilityRange* range,
               std::vector<std::size_t>& visible, VisibilityStats& stats,
               VisibilityQueryState* temporal, const VisibilityBounds* region = nullptr) const;
    void visit(std::size_t index, const VisibilityBounds& region,
               std::vector<std::size_t>& visible, VisibilityStats& stats) const;
    std::vector<VisibilityBounds> bounds_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
    std::vector<std::size_t> leafForInstance_;
};

} // namespace viewer
