#pragma once

#include "model.hpp"
#include "model_visibility.hpp"

#include <limits>

namespace viewer {

inline constexpr std::size_t noBuildingBatch = std::numeric_limits<std::size_t>::max();

struct BuildingBatchDetail {
    // Union of full alternative-group bounds, with the largest switch distance.
    // Being beyond this conservative volume guarantees every member is far.
    VisibilityBounds bounds{glm::vec3(std::numeric_limits<float>::max()),
                            glm::vec3(std::numeric_limits<float>::lowest())};
    float distance = 0;
};

struct BuildingBatches {
    std::vector<Primitive> meshes;
    // Original draw indices remain authoritative for visibility and doors.
    std::vector<std::size_t> batchForDraw;
    std::vector<BuildingBatchDetail> details;
};

// Merge generated static opaque roofs by material and 128 m X/Z cell. Vertices
// are baked into the runtime world frame (X east, Y up, Z south). No collision
// geometry, source transforms, or building detail metadata is modified.
BuildingBatches buildRoofBatches(const Model& model);
// Simple static opaque walls only. Near-cell fallback uses the same group bounds
// as ModelVisibility, including alternative geometry and moving-panel padding.
BuildingBatches buildWallBatches(const Model& model, const ModelVisibility& visibility);

} // namespace viewer
