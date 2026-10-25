#include "action_hotbar.hpp"
#include "inventory/player_inventory.hpp"
#include "action_selection.hpp"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <stdexcept>

namespace viewer {
void populateActionHotbar(Rml::ElementDocument& document, const PlayerInventory& inventory) {
    std::string contents;
    const auto actions = actionShortcuts(inventory);
    for (std::size_t slot = 0; slot < actions.size(); ++slot) {
        const auto& action = actions[slot];
        contents += "<div class='action-slot " + std::string(action.name.empty() ? "empty" : action.skill ? "skill" : "item") +
            (slot == 9 ? " last" : "") + "'><span class='action-number'>" + std::to_string(slot) + "</span>";
        if (action.name.empty()) contents += "<div class='assignment-name'>Unassigned</div>";
        else {
            const auto* item = inventory.find(inventory.hotbar()[slot]);
            const bool explosive = item && item->hasTag("explosive");
            contents += "<span class='assignment-type'>" + std::string(action.skill ? "SKILL" : "ITEM") +
                "</span><div class='assignment-square " + std::string(action.skill ? "punch" : explosive ? "grenade" : "pistol") +
                "'></div><div class='assignment-name'>" + Rml::StringUtilities::EncodeRml(action.name) + "</div>";
        }
        contents += "</div>";
    }
    auto* host = document.GetElementById("hotbar-slots");
    if (!host) throw std::runtime_error("Missing action hotbar host");
    host->SetInnerRML(contents);
}
void populateWeaponSelection(Rml::ElementDocument& document, const PlayerInventory& inventory, int selected) {
    auto* host = document.GetElementById("selection-slots");
    if (!host) throw std::runtime_error("Missing weapon selection host");
    const auto actions = actionShortcuts(inventory);
    std::string contents;
    int left = 0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto& action = actions[i];
        constexpr std::array<const char*, 6> icons{"pistol", "smg", "shotgun", "sniper", "grenade", "fists"};
        const auto icon = action.equipment && *action.equipment < icons.size() ? icons[*action.equipment] : "";
        const bool enlarged = static_cast<int>(i) == selected;
        contents += "<div class='selection-slot" + std::string(enlarged ? " selected" : "") +
            (action.skill ? " skill" : "") + (action.name.empty() ? " empty" : "") +
            "' style='left:" + std::to_string(left) + "dp'><div class='selection-key'>" + std::to_string(i) +
            "</div><div class='selection-icon " + icon + "'></div><div class='selection-name'>" +
            Rml::StringUtilities::EncodeRml(action.name.empty() ? "Unassigned" : action.name) + "</div></div>";
        left += (enlarged ? 112 : 22) + 4;
    }
    host->SetInnerRML(contents);
}
} // namespace viewer
