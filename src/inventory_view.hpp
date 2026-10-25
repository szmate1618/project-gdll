#pragma once

#include <string>

namespace Rml { class ElementDocument; }
namespace viewer {
struct StorageLoadout;
struct InventoryItems;
// Rebuilds storage markup after a placement changes; the held item is omitted.
void populateInventory(Rml::ElementDocument& document, const StorageLoadout& loadout, const InventoryItems& inventory,
                       const std::string& heldItem = {});
void populateActionHotbar(Rml::ElementDocument& document, const InventoryItems& inventory);
}
