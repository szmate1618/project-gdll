#pragma once
#include "model.hpp"

namespace viewer {
struct GuardrailLayer {
    Model model;
    std::size_t ways = 0;
    double lengthMeters = 0;
};
// Load a self-contained, versioned sidecar GLB. Verify the town fingerprint
// before rendering or collision use; geometry is already in runtime Y-up meters.
GuardrailLayer loadGuardrailLayer(const std::filesystem::path& path, const std::filesystem::path& map);
} // namespace viewer
