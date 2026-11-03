#pragma once
#include "hud_pointer.hpp"
#include "inventory/inventory_action.hpp"
#include <vector>
#include <string>
#include <glm/vec2.hpp>

namespace Rml { class ElementDocument; class Element; class Context; }
namespace viewer {
class PlayerInventory;
struct HudState;
struct PresentationViewport;
// Gameplay-only equipment list. Emits simulation actions; owns no items/bodies.
class EquipmentDrawer {
public:
    EquipmentDrawer(Rml::ElementDocument& document, PlayerInventory& inventory);
    void process(const HudState& state, const std::vector<HudPointerEvent>& events,
        int width, int height, const PresentationViewport& presentation);
    std::vector<InventoryDragAction> takeActions();
private:
    bool contains(Rml::Element& element) const;
    void populate();
    Rml::ElementDocument& document_;
    PlayerInventory& inventory_;
    Rml::Element *drawer_, *list_, *status_;
    glm::vec2 pointer_{-1};
    std::string signature_, statusText_;
    bool open_ = false, pressed_ = false;
    std::vector<InventoryDragAction> actions_;
};
}
