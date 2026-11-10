#pragma once

#include "hud_pointer.hpp"
#include "hud_screen.hpp"
#include <glm/vec2.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Rml { class ElementDocument; }
namespace viewer {
struct PresentationViewport;

// Menu chrome only. Physical inventory and world grips retain their existing owners.
class CraftingMenu {
public:
    explicit CraftingMenu(Rml::ElementDocument& document) : document_(document) {}
    void open();
    void close();
    std::vector<HudPointerEvent> process(const std::vector<HudPointerEvent>& events,
        int width, int height, const PresentationViewport& presentation);
    std::optional<HudScreen> takeScreenRequest();
private:
    bool contains(const char* id) const;
    std::string control() const;
    Rml::ElementDocument& document_;
    glm::vec2 pointer_{-1};
    std::string pressed_;
    bool expanded_ = true, claimed_ = false;
    std::optional<HudScreen> requested_;
};
}
