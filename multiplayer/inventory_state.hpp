#pragma once
#include "protocol.hpp"
#include "ground_items.hpp"

namespace multiplayer {
InventoryState captureInventory(const viewer::PlayerInventory& inventory);
bool applyInventory(viewer::PlayerInventory& inventory, const viewer::PlayerInventory& catalog, const InventoryState& state, bool* layoutChanged = nullptr);
std::vector<GroundItemState> captureGround(const viewer::GroundItems& ground, glm::vec3 observer);
void applyGround(viewer::GroundItems& ground, const viewer::PlayerInventory& catalog, const std::vector<GroundItemState>& state);
}
