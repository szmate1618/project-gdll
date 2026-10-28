#pragma once

#include "../command_wheel.hpp"
#include <vector>

namespace Rml { class Element; class ElementDocument; }
namespace viewer {
struct HudSignal {
    SignalKind kind = SignalKind::None;
    glm::vec2 position{0}; // Framebuffer fractions, origin at top left.
    bool atEdge = false;
};

// Borrows the gameplay document, owned by Hud. Uses its context and renderer.
class CommandWheelView {
public:
    explicit CommandWheelView(Rml::ElementDocument& document);
    void update(bool open, glm::vec2 offset, const std::vector<HudSignal>& signals);
private:
    Rml::ElementDocument& document_;
    Rml::Element* wheel_;
    Rml::Element* cursor_;
    Rml::Element* center_;
    Rml::Element* markers_;
    std::vector<Rml::Element*> markers;
};
} // namespace viewer
