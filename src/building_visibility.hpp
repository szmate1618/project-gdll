#pragma once

#include "building_batch.hpp"

namespace viewer {

struct BuildingVisibilityView {
    glm::mat4 projectionView{1}, projection{1};
    glm::vec3 eye{0};
    const HorizontalVisibilityRange* range = nullptr;
    bool frustumCulling = true, roofs = true, walls = true;
};

// Caller-owned scratch and temporal hints; use separate state for each camera.
struct BuildingVisibilityState {
    VisibilityQueryState temporal;
    std::vector<std::size_t> coarse;
};

struct BuildingVisibilityStats {
    VisibilityStats coarse;
    std::size_t selectedBatches = 0, testedMembers = 0, skippedDetails = 0;
    // Selected sources omitted from the representative-only result.
    std::size_t extraSources = 0;
};

// Camera BVH over roof/wall batches and remaining individual scene sources.
// Owns full transformed bounds in world X-east/Y-up/Z-south metres. Nearby wall
// batches expand to original source IDs; distant batches omit facade alternatives.
// Source IDs remain authoritative for detail filtering, material and draw order.
class BuildingVisibility {
public:
    BuildingVisibility() = default;
    BuildingVisibility(const Model& model, const ModelVisibility& source,
                       const BuildingBatches& roofs, const BuildingBatches& walls);
    bool empty() const { return batches_.empty(); }
    BuildingVisibilityStats query(const BuildingVisibilityView& view,
                                  std::vector<std::size_t>& visible,
                                  BuildingVisibilityState& state) const;

private:
    struct Member { VisibilityBounds bounds; std::size_t source; bool simple; };
    struct Batch {
        VisibilityBounds bounds;
        BuildingBatchDetail detail;
        std::size_t first = 0, count = 0, simpleCount = 0, firstSimple = 0;
        bool roof = false;
    };
    VisibilityIndex index_;
    std::vector<std::size_t> items_; // Source ID, or sourceCount_ + batch ID.
    std::vector<Batch> batches_;
    std::vector<Member> members_; // Contiguous per batch, ascending source IDs.
    std::size_t sourceCount_ = 0;
};

} // namespace viewer
