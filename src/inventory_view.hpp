#pragma once

namespace Rml { class ElementDocument; }
namespace viewer {
struct StorageLoadout;
// Builds static storage markup once when Inventory is first opened.
void populateInventory(Rml::ElementDocument& document, const StorageLoadout& loadout);
}
