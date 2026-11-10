#pragma once

#include "inventory/item_model.hpp"
#include "model.hpp"

namespace viewer {
struct ItemModelBounds {
    glm::vec3 minimum{0}, maximum{0};
};

ItemModelBounds itemModelBounds(const ItemModelConfig& config);
// Emits closed, flat-shaded box/frustum parts in local Y-up metres. Draw names
// retain the part names; the source config retains material metadata.
Model buildItemModel(const ItemModelConfig& config);
// Identical immutable definitions share one GPU mesh/batch, including renamed
// inventory stacks. This process-local key is not a resource fingerprint.
std::string itemModelGeometryKey(const ItemModelConfig& config);
} // namespace viewer
