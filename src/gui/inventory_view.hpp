#pragma once

#include <string>

namespace Rml { class ElementDocument; }
namespace viewer {
class PlayerInventory;
// Rebuilds storage markup after a placement changes; the held item is omitted.
void populateInventory(Rml::ElementDocument& document, const PlayerInventory& inventory);
// Updates quantities in place so gameplay consumption keeps selection/drag DOM valid.
void refreshInventoryQuantities(Rml::ElementDocument& document, const PlayerInventory& inventory);
void populateInventoryDetails(Rml::ElementDocument& document, const PlayerInventory& inventory, const std::string& selected);
}
