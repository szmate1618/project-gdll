#include "crafting_menu.hpp"
#include "hud.hpp"
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
    retrieving_ = false;
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
std::string CraftingMenu::itemAtPointer() const {
    if (!contains("crafting-list")) return {};
    auto* list=document_.GetElementById("crafting-list");
    for (int index=0;index<list->GetNumChildren();++index) {
        auto* row=list->GetChild(index);
        const auto origin=row->GetAbsoluteOffset(Rml::BoxArea::Border), size=row->GetBox().GetSize(Rml::BoxArea::Border);
        if (pointer_.x>=origin.x && pointer_.x<origin.x+size.x && pointer_.y>=origin.y && pointer_.y<origin.y+size.y)
            return row->GetAttribute<Rml::String>("data-item","");
    }
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
std::vector<HudPointerEvent> CraftingMenu::process(const HudState& state,const std::vector<HudPointerEvent>& events,
    int width, int height, const PresentationViewport& presentation) {
    populate();
    std::vector<HudPointerEvent> remaining;
    for (const auto& event : events) {
        if (event.action == HudPointerAction::Move)
            pointer_ = presentation.scenePointer(event.x, event.y, width, height);
        const auto* context=document_.GetContext();
        const float edge=6*context->GetDensityIndependentPixelRatio();
        const auto dimensions=context->GetDimensions();
        const bool inside=pointer_.x>=0 && pointer_.y>=0 && pointer_.x<dimensions.x && pointer_.y<dimensions.y;
        const bool inMenu = contains("crafting-drawer") || contains("crafting-header-surface") || contains("crafting-footer") ||
            (inside && (pointer_.x<edge || pointer_.x>=dimensions.x-edge));
        if (event.action == HudPointerAction::Cancel) {
            if (retrieving_) actions_.push_back({InventoryDragActionKind::HandRelease,"",""});
            const bool owned=retrieving_;
            retrieving_=false;
            pressed_.clear(); claimed_ = false;
            if (owned) continue;
        }
        if (event.action == HudPointerAction::Press && inMenu) {
            claimed_ = true;
            pressed_ = control();
            const auto item=itemAtPointer();
            if (!state.mouseCaptured && !item.empty() && !state.handInventoryActive &&
                state.weaponSwitchRemaining<=0 && state.reloadRemaining<=0) {
                actions_.push_back({InventoryDragActionKind::HandPickUp,item,""});
                retrieving_=true;
            }
            continue;
        }
        if (event.action == HudPointerAction::Release && claimed_) {
            const auto pressed = std::exchange(pressed_, {});
            claimed_ = false;
            if (std::exchange(retrieving_,false)) {
                actions_.push_back({contains("crafting-list") || contains("equipment-list") ?
                    InventoryDragActionKind::HandStore : InventoryDragActionKind::HandEndDrag,"",""});
                continue;
            }
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
std::vector<InventoryDragAction> CraftingMenu::takeActions() { return std::exchange(actions_,{}); }
}
