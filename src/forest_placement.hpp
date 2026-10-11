#pragma once
#include "forest_geometry.hpp"
#include <cstdint>
#include <string>

namespace viewer {
struct ForestTreeCandidate {
    glm::dvec2 eastNorth;
    float height = 0, yaw = 0;
};
// Stable integer hashing of patch ID, seed and lattice cell. Independent of
// patch order, runtime camera/LoD and standard-library random distributions.
std::vector<ForestTreeCandidate> forestTreeCandidates(const ForestPolygon& polygon,
    const std::string& patchId, std::uint32_t seed, float spacing,
    float minimumHeight, float maximumHeight);
} // namespace viewer
