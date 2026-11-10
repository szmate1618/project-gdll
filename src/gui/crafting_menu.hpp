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
class PlayerInventory;

// Menu chrome only. Physical inventory and world grips retain their existing owners.
class CraftingMenu {
public:
    CraftingMenu(Rml::ElementDocument& document, const PlayerInventory& inventory)
        : document_(document), inventory_(inventory) {}
    void open();
    void close();
    std::vector<HudPointerEvent> process(const std::vector<HudPointerEvent>& events,
        int width, int height, const PresentationViewport& presentation);
    std::optional<HudScreen> takeScreenRequest();
private:
    bool contains(const char* id) const;
    std::string control() const;
    void populate();
    Rml::ElementDocument& document_;
    const PlayerInventory& inventory_;
    glm::vec2 pointer_{-1};
    std::string pressed_;
    std::string signature_;
    bool expanded_ = true, claimed_ = false;
    std::optional<HudScreen> requested_;
};
}
