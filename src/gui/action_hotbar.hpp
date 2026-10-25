#pragma once

namespace Rml { class ElementDocument; }
namespace viewer {
class PlayerInventory;
void populateActionHotbar(Rml::ElementDocument& document, const PlayerInventory& inventory);
}
