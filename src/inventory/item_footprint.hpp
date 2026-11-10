#pragma once

#include "item_model.hpp"
#include <vector>

namespace viewer {

struct ItemFootprint {
    int width = 1, height = 1; // Portrait grid: middle extent across, longest down.
    glm::vec3 dimensions{0}; // Full box extents in metres: longest, middle, thinnest.
    glm::quat rotation{1, 0, 0, 0}; // Box axes in the input point-cloud frame.
};

struct ItemFootprintReference {
    std::vector<glm::vec3> points; // Model-local metres, including node transforms once.
    int width = 1, height = 1; // Existing authored inventory footprint.
};

// The authored 1.30 m / six-cell broom provides an explicit fallback when a
// fixture/catalog has no multi-cell physical references.
inline constexpr float defaultItemFootprintCellsPerMetre = 6.f / 1.3f;

// Fit multi-cell reference axes to their cell-interval centres, without giving
// minimum-one-cell objects an artificial physical size. Empty references are
// ignored; a catalog without a usable axis returns zero, so callers retain their
// explicit defaultItemFootprintCellsPerMetre fallback.
float calibrateItemFootprint(const std::vector<ItemFootprintReference>& references);

// Approximately minimum-volume oriented box, independent of camera/world axes.
// Ignore its thinnest dimension and round the remaining extents upward to cells.
// Sizes are never capped to any storage/container dimensions. Empty/nonfinite
// clouds or invalid scales throw; a cell count outside int range also throws.
ItemFootprint estimateItemFootprint(const std::vector<glm::vec3>& points,
    float cellsPerMetre = defaultItemFootprintCellsPerMetre);

// A custom mesh replaces primitive visuals. Otherwise use the actual transformed
// box/frustum corners, not a world-axis bounding box or the wider collider box.
std::vector<glm::vec3> itemModelFootprintPoints(const ItemModelConfig& model);

} // namespace viewer
