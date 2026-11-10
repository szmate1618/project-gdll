#include "crafting_menu.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <utility>

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
std::vector<HudPointerEvent> CraftingMenu::process(const std::vector<HudPointerEvent>& events,
    int width, int height, const PresentationViewport& presentation) {
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
        if ((event.action == HudPointerAction::Wheel || event.action == HudPointerAction::RightPress) && inMenu) continue;
        remaining.push_back(event);
    }
    return remaining;
}
std::optional<HudScreen> CraftingMenu::takeScreenRequest() { return std::exchange(requested_, {}); }
}
