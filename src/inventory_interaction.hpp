#pragma once

#include "inventory_drag.hpp"
#include <vector>

namespace Rml { class Element; class ElementDocument; }
namespace viewer {
enum class InventoryPointerAction { Move, Press, Release, Cancel, Wheel };
struct InventoryPointerEvent {
    InventoryPointerAction action;
    float x = 0, y = 0; // Move: window fractions. Wheel: x is signed notches.
};

// RmlUi adapter for storage dragging. Owns the fixed loadout; borrows the
// document and session inventory. Input is consumed on the rendering thread.
class InventoryInteraction {
public:
    InventoryInteraction(Rml::ElementDocument& document, StorageLoadout storage, InventoryItems& inventory);
    void process(const std::vector<InventoryPointerEvent>& events, int width, int height);
    void cancel();
private:
    struct Hit {
        std::string storage, item;
        int x = 0, y = 0;
        float left = 0, top = 0, cellWidth = 0, cellHeight = 0;
    };
    Hit hit() const;
    void refresh();
    void details();
    void ghost();
    void rotate(int turns);
    Rml::ElementDocument& document_;
    StorageLoadout storage_;
    InventoryItems& inventory_;
    InventoryDrag drag_;
    Rml::Element* ghost_ = nullptr; // Owned by the document.
    std::string selected_;
    float normalizedX_ = 0, normalizedY_ = 0, x_ = 0, y_ = 0;
    float wheel_ = 0, cellWidth_ = 16, cellHeight_ = 16;
    int anchorX_ = 0, anchorY_ = 0;
    bool pressed_ = false;
};
} // namespace viewer
