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
struct PendingInventoryPlacement {
    std::string item;
    ItemPlacement destination;
    InventoryDropCheck result;
    double durationSeconds = 0, elapsedSeconds = 0;
    double progress() const { return durationSeconds > 0 ? elapsedSeconds / durationSeconds : 1; }
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
    const auto& pending() const { return pending_; }
    bool pickUp(const std::string& item);
    void rotate(int quarterTurns);
    InventoryDropCheck preview(const std::string& storage, int x, int y) const;
    InventoryDropCheck drop(const std::string& storage, int x, int y);
    // Advance a pending placement with simulation/session time. Returns true
    // when the placement completes. Pickup and cancellation require no time.
    bool advance(double seconds);
    void cancel();
private:
    void completePlacement();
    PlayerInventory& inventory_;
    std::optional<std::map<std::string, ItemPlacement>> originalPlacements_;
    std::optional<PendingInventoryPlacement> pending_;
    double sourceAccessSeconds_ = 0;
};
} // namespace viewer
