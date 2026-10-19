#pragma once

#include "visibility.hpp"
#include <cstdint>

namespace viewer {

// Stable front-to-back bands for alpha-masked vegetation. Only query results
// are visited; source transforms/bounds and order within a band stay unchanged.
class TreeDepthOrder {
public:
    // Unit forward direction; bounds are world-space (X east, Y up, Z south).
    // Uses 8 m camera-depth bands; all depths >=1016 m share the last band.
    void sort(std::vector<std::size_t>& instances, const std::vector<VisibilityBounds>& bounds,
              glm::vec3 eye, glm::vec3 forward);
private:
    // Reused across frames. Only indices move, never instance matrices.
    std::vector<std::size_t> scratch_;
    std::vector<std::uint8_t> bands_;
};

} // namespace viewer
