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
// Gameplay equipment list, pinned while Crafting is open.
// Emits simulation actions; owns no items/bodies.
class EquipmentDrawer {
public:
    EquipmentDrawer(Rml::ElementDocument& document, PlayerInventory& inventory);
    void openForCrafting();
    void process(const HudState& state, const std::vector<HudPointerEvent>& events,
        int width, int height, const PresentationViewport& presentation);
    std::vector<InventoryDragAction> takeActions();
private:
    bool contains(Rml::Element& element) const;
    void populate();
    Rml::ElementDocument& document_;
    PlayerInventory& inventory_;
    Rml::Element *drawer_, *list_, *status_, *tab_, *close_;
    Rml::Element* pressedToggle_ = nullptr;
    glm::vec2 pointer_{-1};
    std::string signature_, statusText_;
    bool open_ = false, pressed_ = false, crafting_ = false;
    std::uint32_t seenRejections_ = 0;
    double rejectionUntil_ = -1;
    std::vector<InventoryDragAction> actions_;
};
}
