#pragma once

#include "inventory/inventory_drag.hpp"
#include "inventory/inventory_action.hpp"
#include "hud_pointer.hpp"
#include "ground_items.hpp"
#include <vector>

namespace Rml { class Element; class ElementDocument; }
namespace viewer {
struct PresentationViewport;
// RmlUi adapter for click-to-move storage and ground transfers. Borrows the document and player inventory. Input is consumed on the rendering thread.
class InventoryInteraction {
public:
    InventoryInteraction(Rml::ElementDocument& document, PlayerInventory& inventory);
    void process(const std::vector<HudPointerEvent>& events, int width, int height,
        const PresentationViewport& presentation, double seconds, glm::vec3 feet = {}, glm::vec3 forward = {0,0,-1});
    void setGround(GroundItems& ground, Weapons& weapons, const CollisionWorld& collision);
    void cancel();
    std::vector<InventoryDragAction> takeActions();
    bool dragging() const { return drag_.held().has_value() || groundHeld_.has_value() || !actions_.empty(); }
    void refreshLayout(); // Refresh a synchronized idle inventory without cancelling input.
private:
    struct Hit {
        std::string storage, item;
        std::uint64_t ground = 0;
        int x = 0, y = 0;
        float left = 0, top = 0, cellWidth = 0, cellHeight = 0;
        Rml::Element* grid = nullptr;
    };
    Hit hit() const;
    void refresh();
    void ghost();
    void rotate(int turns);
    void details();
    void refreshGround();
    bool canTake(const Hit& hit) const;
    GroundItems* ground_ = nullptr;
    Weapons* weapons_ = nullptr;
    const CollisionWorld* collision_ = nullptr;
    InventoryContainer groundLayout_;
    std::vector<GroundItem> nearby_;
    std::optional<GroundItem> groundHeld_;
    std::string groundSignature_;
    int groundRotation_ = 0;
    glm::vec3 feet_{0}, forward_{0,0,-1};
    Rml::ElementDocument& document_;
    PlayerInventory& inventory_;
    InventoryDrag drag_;
    std::vector<InventoryDragAction> actions_;
    Rml::Element* ghost_ = nullptr; // Owned by the document.
    Rml::Element* progress_ = nullptr;
    Rml::Element* progressFill_ = nullptr;
    Rml::Element* progressLabel_ = nullptr;
    Rml::Element* destinationGrid_ = nullptr; // Valid until refresh rebuilds the grids.
    std::optional<double> lastSeconds_;
    float normalizedX_ = 0, normalizedY_ = 0, x_ = 0, y_ = 0;
    float wheel_ = 0, cellWidth_ = 16, cellHeight_ = 16;
    int anchorX_ = 0, anchorY_ = 0;
};
} // namespace viewer
