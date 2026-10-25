#pragma once

namespace Rml { class ElementDocument; }
namespace viewer {
struct StorageLoadout;
struct InventoryItems;
// Builds static storage markup once when Inventory is first opened.
void populateInventory(Rml::ElementDocument& document, const StorageLoadout& loadout);
void populateActionHotbar(Rml::ElementDocument& document, const InventoryItems& inventory);
}
