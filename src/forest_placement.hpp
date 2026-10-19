#pragma once
#include "forest_geometry.hpp"
#include <cstdint>
#include <string>

namespace viewer {
struct ForestTreeCandidate {
    glm::dvec2 eastNorth;
    float height = 0, yaw = 0;
    bool oldForest = false;
};
// Stable integer hashing of patch ID, seed and lattice cell. Independent of
// patch order, runtime camera/LoD and standard-library random distributions.
std::vector<ForestTreeCandidate> forestTreeCandidates(const ForestPolygon& polygon,
    const std::string& patchId, std::uint32_t seed, float spacing,
    float minimumHeight, float maximumHeight);
// Preserve the original lattice, enlarge old-forest trees, and add an
// independent 20%-density lattice only inside the original AND contracted ring.
std::vector<ForestTreeCandidate> oldForestTreeCandidates(const ForestPolygon& polygon,
    const ForestPolygon& oldForest, const std::string& patchId, std::uint32_t seed,
    float spacing, float minimumHeight, float maximumHeight);

struct ForestBushCandidate {
    glm::dvec2 eastNorth;
    float height = 0, yaw = 0; // Height 1.5..3.5 m; width/depth are four times height.
};
// 60% primary chance; conditional 20% chance of two non-recursive neighbors
// uniform in a 5 m radius disk. Callers apply polygon/building/ground exclusions.
std::vector<ForestBushCandidate> forestBushCandidates(glm::dvec2 tree,
    const std::string& treeId, std::uint32_t seed);
} // namespace viewer
