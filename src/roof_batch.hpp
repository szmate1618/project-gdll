#pragma once

#include "model.hpp"

#include <limits>

namespace viewer {

inline constexpr std::size_t noRoofBatch = std::numeric_limits<std::size_t>::max();

struct RoofBatches {
    std::vector<Primitive> meshes;
    // Original draw indices remain authoritative for visibility and doors.
    std::vector<std::size_t> batchForDraw;
};

// Merge generated static opaque roofs by material and 128 m X/Z cell. Vertices
// are baked into the runtime world frame (X east, Y up, Z south). No collision
// geometry, source transforms, or building detail metadata is modified.
RoofBatches buildRoofBatches(const Model& model);

} // namespace viewer
