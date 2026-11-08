#include "mission_menu.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <utility>

namespace viewer {
void MissionMenu::open() {
    confirming_ = false; selected_ = 0; action_.reset(); pressed_.clear();
    document_.SetClass("confirming", false); updateFocus();
}
void MissionMenu::updateFocus() {
    const char* chosen = confirming_ ? (selected_ ? "mission-confirm" : "mission-cancel") :
        (selected_ ? "mission-abandon" : "mission-resume");
    for (const auto* id : {"mission-resume", "mission-abandon", "mission-cancel", "mission-confirm"})
        document_.GetElementById(id)->SetPseudoClass("focus", std::string(id) == chosen);
}
void MissionMenu::activate(const std::string& id) {
    if (id == "mission-resume") action_ = MissionMenuAction::Resume;
    else if (id == "mission-confirm") action_ = MissionMenuAction::Abandon;
    else {
        confirming_ = id == "mission-abandon"; selected_ = 0;
        document_.SetClass("confirming", confirming_); updateFocus();
    }
}
void MissionMenu::key(MissionMenuKey key) {
    if (key == MissionMenuKey::Activate)
        activate(confirming_ ? (selected_ ? "mission-confirm" : "mission-cancel") :
            (selected_ ? "mission-abandon" : "mission-resume"));
    else { selected_ = 1 - selected_; updateFocus(); }
}
bool MissionMenu::cancelConfirmation() {
    if (!confirming_) return false;
    open(); return true;
}
std::optional<MissionMenuAction> MissionMenu::takeAction() { return std::exchange(action_, {}); }
void MissionMenu::process(const std::vector<HudPointerEvent>& events, int width, int height,
                          const PresentationViewport& presentation) {
    const auto hit = [&](const char* id) {
        auto* element = document_.GetElementById(id);
        if (!element || !element->IsVisible(true)) return false;
        const auto p = presentation.scenePointer(pointer_.x, pointer_.y, width, height);
        const auto low = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
        return p.x >= low.x && p.y >= low.y && p.x <= low.x + size.x && p.y <= low.y + size.y;
    };
    for (const auto& event : events) {
        if (event.action == HudPointerAction::Move) pointer_ = {event.x, event.y};
        if (event.action == HudPointerAction::Cancel) pressed_.clear();
        if (event.action == HudPointerAction::Press) {
            pressed_.clear();
            for (const auto* id : {"mission-resume", "mission-abandon", "mission-cancel", "mission-confirm"})
                if (hit(id)) { pressed_ = id; break; }
        }
        if (event.action != HudPointerAction::Release) continue;
        if (!pressed_.empty() && hit(pressed_.c_str())) {
            activate(pressed_);
        }
        pressed_.clear();
    }
    for (const auto* id : {"mission-resume", "mission-abandon", "mission-cancel", "mission-confirm"})
        document_.GetElementById(id)->SetPseudoClass("hover", hit(id));
}
}
