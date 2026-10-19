#pragma once

#include "forest_layer.hpp"
#include <optional>

namespace viewer {

// Built once from forest polygons, independently of tree population. Camera
// membership uses world X/Z -> east/north (X,-Z), within patch height bounds.
class OldForestVisibility {
public:
    static constexpr float distance = 100;
    OldForestVisibility() = default;
    explicit OldForestVisibility(const std::vector<ForestPatch>& patches);
    std::optional<HorizontalVisibilityRange> rangeAt(glm::vec3 camera) const;
private:
    std::vector<ForestPolygon> areas_;
    VisibilityIndex index_;
};

} // namespace viewer
