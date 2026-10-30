#pragma once
#include "hud.hpp"
#include "hud_pointer.hpp"
#include <glm/vec2.hpp>

namespace Rml { class Context; class ElementDocument; }
namespace viewer {
struct PresentationViewport;
// Routes page-tab clicks before inventory/map gestures. The caller owns the
// selected screen; clicking the current tab keeps that page open.
class LoadoutNavigation {
public:
    explicit LoadoutNavigation(Rml::Context& context) : context_(context) {}
    std::vector<HudPointerEvent> process(Rml::ElementDocument& document, HudScreen screen,
        const std::vector<HudPointerEvent>& events, int width, int height, const PresentationViewport& presentation);
    std::optional<HudScreen> takeRequest();
    void cancel();
private:
    std::optional<HudScreen> hoveredTab(Rml::ElementDocument& document) const;
    Rml::Context& context_;
    glm::vec2 pointer_{-1}; // Window fractions, remapped after resize.
    std::optional<HudScreen> pressedTab_, requested_;
};
}
