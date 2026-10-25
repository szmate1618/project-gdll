#pragma once

#include <glm/glm.hpp>
#include <optional>

namespace viewer {
struct Model;

struct MinimapSource {
    int texture = -1; // Index into the loaded scene model's textures.
    glm::dmat3 uvFromWorld{1}; // (runtime X east, Z south, 1) -> native glTF (u, v, 1).
    glm::ivec2 imageSize{1, 1}; // Published texture pixels, for fitting the full-image view.
};

// Inspect the active terrain once at startup. Reject missing imagery, ambiguous
// terrain, tiled UVs and non-affine mappings rather than showing a wrong map.
std::optional<MinimapSource> findMinimapSource(const Model& model);
} // namespace viewer
