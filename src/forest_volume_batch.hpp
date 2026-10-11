#pragma once

#include "forest_layer.hpp"

namespace viewer {

struct ForestVolumeRange {
    std::size_t patch = 0;
    std::size_t firstIndex = 0, indexCount = 0;
};

struct BatchedForestVolumes {
    Model model;
    // One list per material primitive in model, in source patch order. Geometry
    // remains static; each frame submits only ranges of visible volume patches.
    std::vector<std::vector<ForestVolumeRange>> ranges;
};

// Bakes draw transforms once into world-space vertices and groups geometry by
// material. Reflection reverses triangle winding; UVs and colors are preserved.
BatchedForestVolumes batchForestVolumes(const ForestLayer& forests);

} // namespace viewer
