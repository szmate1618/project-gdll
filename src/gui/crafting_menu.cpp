#include "crafting_menu.hpp"
#include "inventory/player_inventory.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <utility>
#include <sstream>

namespace viewer {
void CraftingMenu::open() {
    expanded_ = true;
    document_.SetClass("crafting-expanded", true);
    close();
}
void CraftingMenu::close() {
    pressed_.clear();
    claimed_ = false;
    requested_.reset();
}
bool CraftingMenu::contains(const char* id) const {
    auto* element = document_.GetElementById(id);
    if (!element || !element->IsVisible(true)) return false;
    const auto origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return pointer_.x >= origin.x && pointer_.x < origin.x + size.x &&
        pointer_.y >= origin.y && pointer_.y < origin.y + size.y;
}
std::string CraftingMenu::control() const {
    for (const char* id : {"crafting-tab-handle", "crafting-collapse", "crafting-close"})
        if (contains(id)) return id;
    return {};
}
void CraftingMenu::populate() {
    std::ostringstream markup;
    const auto encode = [](const std::string& value) { return Rml::StringUtilities::EncodeRml(value); };
    for (const auto& [id, item] : inventory_.items()) {
        const bool crafting = item.hasTag("crafting"), bind = item.hasTag("bind");
        if (!crafting && !bind) continue;
        markup << "<div class=\"crafting-item\" data-item=\"" << encode(id) << "\">"
               << "<div class=\"crafting-item-name\">" << encode(item.inventory().name) << "</div>";
        if (item.inventory().stackable)
            markup << "<div class=\"crafting-quantity\">" << formatItemQuantity(item.inventory(), inventory_.quantity(id)) << "</div>";
        markup << "<div class=\"crafting-item-tags\">" << (crafting ? "CRAFTING" : "")
               << (crafting && bind ? " · " : "") << (bind ? "BIND" : "") << "</div></div>";
    }
    auto html = markup.str();
    if (html.empty()) html = "<div class=\"crafting-empty\">No crafting items</div>";
    if (signature_ != html) {
        signature_ = std::move(html);
        document_.GetElementById("crafting-list")->SetInnerRML(signature_);
    }
}
std::vector<HudPointerEvent> CraftingMenu::process(const std::vector<HudPointerEvent>& events,
    int width, int height, const PresentationViewport& presentation) {
    populate();
    std::vector<HudPointerEvent> remaining;
    for (const auto& event : events) {
        if (event.action == HudPointerAction::Move)
            pointer_ = presentation.scenePointer(event.x, event.y, width, height);
        const bool inMenu = contains("crafting-drawer") || contains("crafting-header-surface") || contains("crafting-footer");
        if (event.action == HudPointerAction::Cancel) {
            pressed_.clear(); claimed_ = false;
        }
        if (event.action == HudPointerAction::Press && inMenu) {
            claimed_ = true;
            pressed_ = control();
            continue;
        }
        if (event.action == HudPointerAction::Release && claimed_) {
            const auto pressed = std::exchange(pressed_, {});
            claimed_ = false;
            if (!pressed.empty() && pressed == control()) {
                if (pressed == "crafting-close") requested_ = HudScreen::Gameplay;
                else {
                    expanded_ = !expanded_;
                    document_.SetClass("crafting-expanded", expanded_);
                }
            }
            continue;
        }
        if (event.action == HudPointerAction::Wheel && contains("crafting-list")) {
            auto* list = document_.GetElementById("crafting-list");
            list->SetScrollTop(list->GetScrollTop() - event.x * 48 * document_.GetContext()->GetDensityIndependentPixelRatio());
        }
        if ((event.action == HudPointerAction::Wheel || event.action == HudPointerAction::RightPress) && inMenu) continue;
        remaining.push_back(event);
    }
    return remaining;
}
std::optional<HudScreen> CraftingMenu::takeScreenRequest() { return std::exchange(requested_, {}); }
}
