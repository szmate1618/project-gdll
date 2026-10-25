#pragma once

#include "inventory_placement.hpp"
#include "player_inventory.hpp"
#include <optional>

namespace viewer {
enum class InventoryDrop { Rejected, Free, Swap };
struct InventoryDropCheck {
    InventoryDrop result = InventoryDrop::Rejected;
    std::string displaced;
};
// Borrows the player inventory, which must outlive this controller. An unfinished swap chain is a reversible transaction.
class InventoryDrag {
public:
    explicit InventoryDrag(PlayerInventory& inventory);
    ~InventoryDrag();
    InventoryDrag(const InventoryDrag&) = delete;
    InventoryDrag& operator=(const InventoryDrag&) = delete;
    const std::optional<HeldInventoryItem>& held() const { return inventory_.held(); }
    const InventoryLayout& layout() const { return inventory_.layout(); }
    bool pickUp(const std::string& item);
    void rotate(int quarterTurns);
    InventoryDropCheck preview(const std::string& storage, int x, int y) const;
    InventoryDropCheck drop(const std::string& storage, int x, int y);
    void cancel();
private:
    PlayerInventory& inventory_;
    std::optional<std::map<std::string, ItemPlacement>> originalPlacements_;
};
} // namespace viewer
