#include "action_selection.hpp"
#include "weapons.hpp"
#include <algorithm>

namespace viewer {
ActionShortcuts actionShortcuts(const PlayerInventory& inventory) {
    ActionShortcuts actions;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto& id = inventory.hotbar()[i];
        if (id.compare(0, 6, "skill:") == 0) {
            const auto skill = id.substr(6);
            actions[i] = {inventory.skills().at(skill), true, skill == "punch" ?
                std::optional<std::size_t>(punchSlot) : std::nullopt};
        } else if (const auto* item = inventory.find(id)) {
            const auto weapon = std::find(weaponIds.begin(), weaponIds.end(), id);
            actions[i] = {item->inventory().name, false, weapon == weaponIds.end() ? std::nullopt :
                std::optional<std::size_t>(static_cast<std::size_t>(weapon - weaponIds.begin()))};
        }
    }
    return actions;
}
void ActionSelection::highlight(int slot) { if (slot >= 0 && slot < 10) selected_ = slot; }
void ActionSelection::cycle(int direction, const ActionShortcuts& actions, std::size_t equipped) {
    if (!direction) return;
    int start = selected_;
    if (start < 0) {
        const auto current = std::find_if(actions.begin(), actions.end(), [&](const auto& a) { return a.equipment == equipped; });
        start = current == actions.end() ? (direction > 0 ? 9 : 0) : static_cast<int>(current - actions.begin());
    }
    for (int offset = 1; offset <= 10; ++offset) {
        const int next = (start + (direction > 0 ? offset : -offset) + 10) % 10;
        if (!actions[next].name.empty()) { selected_ = next; return; }
    }
}
std::optional<std::size_t> ActionSelection::confirm(const ActionShortcuts& actions, std::size_t equipped) {
    if (!open()) return {};
    const auto target = actions[static_cast<std::size_t>(selected_)].equipment;
    close();
    return target && *target != equipped ? target : std::nullopt;
}
} // namespace viewer
