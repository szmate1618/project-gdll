#pragma once

#include "model.hpp"

namespace viewer {

// Convert the sourced garden outline into this published map's world X/Z frame.
// Returns empty for unreferenced scenes, other CRSs, or maps outside the park.
std::vector<glm::vec2> zombieParkBoundary(const Model& scene,
                                        const std::filesystem::path& region);

} // namespace viewer
