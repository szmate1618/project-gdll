#pragma once
#include "forest_geometry.hpp"
#include "tree_layer.hpp"
#include "visibility.hpp"

namespace viewer {
class CollisionWorld;
struct ForestPatch {
    std::string id;
    VisibilityBounds bounds;
    std::size_t firstTree = 0, treeCount = 0; // Indices in combined TreeLayer.
    std::size_t firstDraw = 0, drawCount = 0; // Indices in volumes.draws.
    std::vector<std::size_t> detectedTrees; // Existing trees within this footprint.
    ForestPolygon oldForest{}; // Contracted exterior; intersect with original footprint.
};
struct ForestLayer {
    // Volume material order: side walls, then the horizontal canopy.
    static constexpr std::size_t canopyMaterialIndex = 1;
    Model volumes;
    std::vector<ForestPatch> patches;
    float detailDistance = 350;
};
// Validates map identity/frame, creates all placements once and appends them to
// the shared tree layer. An error leaves the supplied trees unchanged.
ForestLayer loadForestLayer(const std::filesystem::path& config,
    const std::filesystem::path& map, const Model& scene,
    const CollisionWorld& placementWorld, TreeLayer& trees);
bool forestUsesTrees(const ForestPatch& patch, glm::vec3 eye,
    bool scoped, float detailDistance);
} // namespace viewer
