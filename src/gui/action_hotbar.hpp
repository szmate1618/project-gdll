#pragma once
#include <cstddef>

namespace Rml { class ElementDocument; }
namespace viewer {
class PlayerInventory;
const char* equipmentIconClass(std::size_t equipment);
void populateActionHotbar(Rml::ElementDocument& document, const PlayerInventory& inventory);
void populateWeaponSelection(Rml::ElementDocument& document, const PlayerInventory& inventory, int selected);
}
