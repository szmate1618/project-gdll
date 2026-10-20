#pragma once
#include "forest_geometry.hpp"
#include <cstdint>
#include <functional>
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

struct ForestShrubCandidate {
    glm::dvec2 eastNorth;
    float height = 0, yaw = 0; // Height 1.5..3.5 m; width/depth are four times height.
    bool dry = false; // Stable 25% subset; independent of positions and sizes.
};
// 60% primary chance; conditional 20% chance of two non-recursive neighbors
// uniform in a 5 m radius disk. Callers apply polygon/building/ground exclusions.
std::vector<ForestShrubCandidate> forestShrubCandidates(glm::dvec2 tree,
    const std::string& treeId, std::uint32_t seed);

struct ForestDryTreeCandidate {
    glm::dvec2 eastNorth;
    float height = 0, yaw = 0; // 3.5..6 m, 0..2*pi, preserving the asset's lean.
    std::size_t primaryIndex = 0;
    bool companion = false;
};
// Add round(9% * accepted healthy old-forest trees), plus a conditional 10%
// chance of ONE companion in a 1.5..5 m annulus. Companions do not recurse.
// Both polygon constraints and the caller's ground/building/existing-tree
// exclusions apply before publication. An unfillable primary quota throws;
// companions with no valid nearby spot are omitted after bounded retries.
std::vector<ForestDryTreeCandidate> forestDryTreeCandidates(const ForestPolygon& polygon,
    const ForestPolygon& oldForest, const std::string& patchId, std::uint32_t seed,
    std::size_t oldTreeCount,
    const std::function<bool(const ForestDryTreeCandidate&)>& usable);
} // namespace viewer
