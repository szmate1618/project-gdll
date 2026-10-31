#include "loadout_navigation.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <array>
#include <utility>

namespace viewer {
std::optional<HudScreen> LoadoutNavigation::hoveredTab(Rml::ElementDocument& document) const {
    constexpr std::array<const char*, 4> tabs{"inventory-tab", "skills-tab", "health-tab", "map-tab"};
    for (auto* element = context_.GetHoverElement(); element && element != &document; element = element->GetParentNode())
        for (std::size_t i = 0; i < tabs.size(); ++i)
            if (element->GetId() == tabs[i]) return static_cast<HudScreen>(i + 1);
    return {};
}
std::vector<HudPointerEvent> LoadoutNavigation::process(Rml::ElementDocument& document, HudScreen screen,
    const std::vector<HudPointerEvent>& events, int width, int height, const PresentationViewport& presentation) {
    const auto move = [&] {
        const auto point = presentation.scenePointer(pointer_.x, pointer_.y, width, height);
        context_.ProcessMouseMove(static_cast<int>(point.x), static_cast<int>(point.y), 0);
    };
    move();
    std::vector<HudPointerEvent> remaining;
    for (const auto& event : events) {
        if (event.action == HudPointerAction::Move) { pointer_ = {event.x, event.y}; move(); }
        if (requested_) continue; // A completed tab click owns the rest of this input batch.
        switch (event.action) {
        case HudPointerAction::Press:
            pressedTab_ = hoveredTab(document);
            context_.ProcessMouseButtonDown(0, 0);
            if (pressedTab_) continue;
            break;
        case HudPointerAction::Release: {
            context_.ProcessMouseButtonUp(0, 0);
            const auto pressed = std::exchange(pressedTab_, {});
            if (!pressed) break;
            if (pressed == hoveredTab(document) && *pressed != screen) {
                requested_ = pressed;
                // Cancel a held item/pending placement or map gesture before
                // its time advances, even though the caller switches next frame.
                remaining.push_back({HudPointerAction::Cancel});
            }
            continue;
        }
        case HudPointerAction::Cancel: cancel(); break;
        case HudPointerAction::Wheel:
            if (screen != HudScreen::Inventory) context_.ProcessMouseWheel(-event.x, 0);
            break;
        default: break;
        }
        remaining.push_back(event);
    }
    return remaining;
}
std::optional<HudScreen> LoadoutNavigation::takeRequest() { return std::exchange(requested_, {}); }
void LoadoutNavigation::cancel() {
    pressedTab_.reset(); requested_.reset();
    context_.ProcessMouseButtonUp(0, 0);
    context_.ProcessMouseLeave();
}
}
