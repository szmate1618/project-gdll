#include "action_hotbar.hpp"
#include "inventory/player_inventory.hpp"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <stdexcept>

namespace viewer {
void populateActionHotbar(Rml::ElementDocument& document, const PlayerInventory& inventory) {
    std::string contents;
    for (std::size_t slot = 0; slot < inventory.hotbar().size(); ++slot) {
        const auto& id = inventory.hotbar()[slot];
        contents += "<div class='action-slot " + std::string(id.empty() ? "empty" : "item") +
            (slot == 9 ? " last" : "") + "'><span class='action-number'>" + std::to_string(slot) + "</span>";
        if (id.empty()) contents += "<div class='assignment-name'>Unassigned</div>";
        else {
            const auto& item = inventory.items().at(id);
            const bool explosive = std::find(item.tags().begin(), item.tags().end(), "explosive") != item.tags().end();
            contents += "<span class='assignment-type'>ITEM</span><div class='assignment-square " +
                std::string(explosive ? "grenade" : "pistol") + "'></div><div class='assignment-name'>" +
                Rml::StringUtilities::EncodeRml(item.inventory().name) + "</div>";
        }
        contents += "</div>";
    }
    auto* host = document.GetElementById("hotbar-slots");
    if (!host) throw std::runtime_error("Missing action hotbar host");
    host->SetInnerRML(contents);
}
} // namespace viewer
