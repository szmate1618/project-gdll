#pragma once

#include "model.hpp"

namespace viewer {

enum class PoleKind { mapped, hevCatenary };

struct PoleColumn {
    std::string id;
    // Base center in runtime meters: X east, Y up, Z south.
    glm::vec3 position{0};
    float height = 7, width = .2f;
    PoleKind kind = PoleKind::mapped;
};

struct PoleLayer {
    std::vector<PoleColumn> columns;
    Model batches;
};

// Static cuboids grouped into 128 m X/Z cells and grey/green materials;
// regular town culling, shadows and collision operate on the resulting meshes.
Model buildPoleBatches(const std::vector<PoleColumn>& columns);
// Validate a versioned manifest against the published map SHA-256 before use.
PoleLayer loadPoleLayer(const std::filesystem::path& manifest, const std::filesystem::path& map);
// Transfer owned untextured batches into the town before collision/render setup.
void appendPoleLayer(Model& town, PoleLayer&& layer);

} // namespace viewer
