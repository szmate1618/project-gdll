#pragma once

#include "inventory_placement.hpp"
#include "item_config.hpp"
#include <optional>

namespace viewer {
enum class InventoryDrop { Rejected, Free, Swap };
struct InventoryDropCheck {
    InventoryDrop result = InventoryDrop::Rejected;
    std::string displaced;
};
struct HeldInventoryItem {
    std::string item;
    int rotation = 0;
};

// Borrows a fixed storage loadout and mutable session inventory. Both must
// outlive this controller. An unfinished swap chain is a reversible transaction.
class InventoryDrag {
public:
    InventoryDrag(const StorageLoadout& storage, InventoryItems& inventory);
    const std::optional<HeldInventoryItem>& held() const { return held_; }
    const InventoryLayout& layout() const { return layout_; }
    bool pickUp(const std::string& item);
    void rotate(int quarterTurns);
    InventoryDropCheck preview(const std::string& storage, int x, int y) const;
    InventoryDropCheck drop(const std::string& storage, int x, int y);
    void cancel();
private:
    void rebuild();
    const StorageLoadout& storage_;
    InventoryItems& inventory_;
    InventoryLayout layout_;
    std::optional<HeldInventoryItem> held_;
    std::optional<std::map<std::string, ItemPlacement>> originalPlacements_;
};
} // namespace viewer
