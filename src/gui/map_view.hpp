#pragma once

#include "hud_pointer.hpp"
#include "../map_view.hpp"
#include <string>

namespace Rml { class Element; class ElementDocument; }
namespace viewer {
class HudRenderInterface;
struct HudState;
struct PresentationViewport;
void registerMapViewElement();
// Owns local presentation/measurement state; borrows the HUD document/backend.
class MapViewUi {
public:
    MapViewUi(Rml::ElementDocument& document, HudRenderInterface& renderer);
    void setSource(const std::optional<MinimapSource>& source);
    void process(const std::vector<HudPointerEvent>& events, int width, int height,
                 const PresentationViewport& presentation, const HudState& state);
    void cancel() { view_.cancel(); pressedControl_.clear(); }
private:
    void updateLabels();
    Rml::ElementDocument& document_;
    Rml::Element* map_ = nullptr;
    Rml::Element* total_ = nullptr;
    Rml::Element* hint_ = nullptr;
    MapView view_;
    std::vector<MapPlayer> players_;
    std::vector<Rml::Element*> labels_;
    glm::vec2 normalizedPointer_{-1};
    std::string pressedControl_;
};
} // namespace viewer
