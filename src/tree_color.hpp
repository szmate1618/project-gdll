#pragma once

#include "model.hpp"

namespace viewer {
// Mean linear RGB of visible base-color texels, including the material factor.
// Alpha-mask padding is excluded; missing textures/empty masks return nullopt.
std::optional<glm::vec3> visibleTextureColor(const Model& model, const Material& material);

// Returns runtime material copies whose visible texture means match target.
// Leaves alpha, untextured materials and the source model unchanged.
std::vector<Material> matchTreeTextureColors(const Model& model, glm::vec3 target);
} // namespace viewer
