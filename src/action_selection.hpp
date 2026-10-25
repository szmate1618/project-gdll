#pragma once

#include "inventory/player_inventory.hpp"
#include <array>

namespace viewer {
struct ActionShortcut {
    std::string name;
    bool skill = false;
    std::optional<std::size_t> equipment; // Empty for unsupported skill mechanics.
};
using ActionShortcuts = std::array<ActionShortcut, 10>;
ActionShortcuts actionShortcuts(const PlayerInventory& inventory);

// Previewing never equips anything. Mouse wheel skips unassigned slots;
// number keys may highlight any of the ten positions. Confirm consumes once.
class ActionSelection {
public:
    void highlight(int slot);
    void cycle(int direction, const ActionShortcuts& actions, std::size_t equipped);
    std::optional<std::size_t> confirm(const ActionShortcuts& actions, std::size_t equipped);
    void close() { selected_ = -1; }
    int selected() const { return selected_; }
    bool open() const { return selected_ >= 0; }
private:
    int selected_ = -1;
};
} // namespace viewer
