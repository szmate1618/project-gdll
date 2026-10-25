#pragma once

#include <glm/vec2.hpp>
#include <cstddef>
#include <vector>

namespace viewer {
struct Model;

struct MinimapRoadBatch {
    std::size_t first = 0, count = 0;
    glm::vec2 minimum{0}, maximum{0}; // Actual X/Z bounds, including cell-crossing triangles.
};
struct MinimapRoadGeometry {
    std::vector<glm::vec2> vertices; // Flat triangle list, runtime X east / Z south, metres.
    std::vector<MinimapRoadBatch> batches;
};

// Snapshot the loaded roads once; apply node transforms once and group by
// 100 m centroid cells for spatial culling. No imagery or terrain queries.
MinimapRoadGeometry prepareMinimapRoads(const Model& model);
} // namespace viewer
