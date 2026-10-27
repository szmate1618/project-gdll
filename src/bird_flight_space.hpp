#pragma once

#include "visibility.hpp"

namespace viewer {
class CollisionWorld;
struct TreeLayer;

// Static canopy BVH plus the existing collision BVHs. Borrows world for its
// lifetime; canopy bounds are copied once and never depend on render visibility.
class BirdFlightSpace {
public:
    BirdFlightSpace(const CollisionWorld& world, const TreeLayer* trees);
    // One center ray through scene geometry, with conservative canopy blocking.
    bool lineOfSight(glm::vec3 from, glm::vec3 to) const;
    bool clearPoint(glm::vec3 point, float radius = .2f) const;
    bool clearSegment(glm::vec3 from, glm::vec3 to, float radius = .2f) const;
private:
    const CollisionWorld& world_;
    std::vector<VisibilityBounds> canopies_;
    VisibilityIndex canopyIndex_;
    mutable std::vector<std::size_t> candidates_;
};
} // namespace viewer
