#pragma once

#include "inventory/inventory_drag.hpp"
#include "inventory/inventory_action.hpp"
#include <vector>

namespace Rml { class Element; class ElementDocument; }
namespace viewer {
struct PresentationViewport;
enum class InventoryPointerAction { Move, Press, Release, Cancel, Wheel };
struct InventoryPointerEvent {
    InventoryPointerAction action;
    float x = 0, y = 0; // Move: window fractions. Wheel: x is signed notches.
};

// RmlUi adapter for storage dragging. Borrows the document and player inventory. Input is consumed on the rendering thread.
class InventoryInteraction {
public:
    InventoryInteraction(Rml::ElementDocument& document, PlayerInventory& inventory);
    void process(const std::vector<InventoryPointerEvent>& events, int width, int height,
        const PresentationViewport& presentation, double seconds);
    void cancel();
    std::vector<InventoryDragAction> takeActions();
    bool dragging() const { return drag_.held().has_value() || !actions_.empty(); }
    void refreshLayout(); // Refresh a synchronized idle inventory without cancelling input.
private:
    struct Hit {
        std::string storage, item;
        int x = 0, y = 0;
        float left = 0, top = 0, cellWidth = 0, cellHeight = 0;
        Rml::Element* grid = nullptr;
    };
    Hit hit() const;
    void refresh();
    void ghost();
    void rotate(int turns);
    void beginDrag();
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
    std::string selected_;
    std::string dragCandidate_;
    float pressX_ = 0, pressY_ = 0; // Window fractions, stable across viewport resizing.
    float normalizedX_ = 0, normalizedY_ = 0, x_ = 0, y_ = 0;
    float wheel_ = 0, cellWidth_ = 16, cellHeight_ = 16;
    int anchorX_ = 0, anchorY_ = 0;
    bool pressed_ = false;
};
} // namespace viewer
