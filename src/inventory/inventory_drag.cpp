#include "inventory_drag.hpp"
#include <cmath>
#include <algorithm>

namespace viewer {
InventoryDrag::InventoryDrag(PlayerInventory& inventory) : inventory_(inventory) {}
InventoryDrag::~InventoryDrag() { cancel(); }
bool InventoryDrag::pickUp(const std::string& item) {
    if (inventory_.held_) return false;
    const auto found = inventory_.contents_.placements.find(item);
    if (found == inventory_.contents_.placements.end()) return false;
    originalPlacements_ = inventory_.contents_.placements;
    sourceStorage_ = found->second.storage;
    sourceAccess_ = inventoryAccess(inventory_.layout_.at(found->second.storage),
        inventoryRectangle(inventory_.contents_.owned.at(item), found->second));
    inventory_.held_ = HeldInventoryItem{item, found->second.rotation};
    inventory_.contents_.placements.erase(found);
    inventory_.rebuild();
    return true;
}
void InventoryDrag::rotate(int quarterTurns) {
    if (originalPlacements_ && inventory_.held_ && !pending_)
        inventory_.held_->rotation = ((inventory_.held_->rotation / 90 + quarterTurns % 4 + 4) % 4) * 90;
}
InventoryDropCheck InventoryDrag::preview(const std::string& storage, int x, int y) const {
    if (!originalPlacements_ || !inventory_.held_ || pending_) return {};
    const auto found = inventory_.layout_.find(storage);
    if (found == inventory_.layout_.end()) return {};
    if (!inventoryItemAllowed(found->second.grid, inventory_.contents_.owned.at(inventory_.held_->item))) return {};
    const auto rectangle = inventoryRectangle(inventory_.contents_.owned.at(inventory_.held_->item), {storage, x, y, inventory_.held_->rotation});
    if (!inventoryRectangleFits(found->second.grid, rectangle)) return {};
    InventoryDropCheck check{InventoryDrop::Free, {}};
    for (const auto& other : found->second.items) {
        if (!inventoryRectanglesOverlap(rectangle, other)) continue;
        if (!check.displaced.empty()) return {}; // More than one occupied item.
        check = {InventoryDrop::Swap, other.item};
    }
    return check;
}
InventoryDropCheck InventoryDrag::drop(const std::string& storage, int x, int y) {
    const auto check = preview(storage, x, y);
    if (check.result == InventoryDrop::Rejected) return check;
    const ItemPlacement destination{storage, x, y, inventory_.held_->rotation};
    const auto& container = inventory_.layout_.at(storage);
    const auto rectangle = inventoryRectangle(inventory_.contents_.owned.at(inventory_.held_->item), destination);
    InventoryAccess access;
    if (check.result == InventoryDrop::Swap) {
        const auto displaced = std::find_if(container.items.begin(), container.items.end(),
            [&](const auto& other) { return other.item == check.displaced; });
        access = inventorySwapAccess(container, rectangle, *displaced);
    } else access = inventoryAccess(container, rectangle);
    const auto& backpack = inventory_.storage_.backpack;
    const bool mainBackpack = backpack && storage == backpackStorageReference(backpack->id);
    // Returning to the same holster/pouch/pocket is a free put-back, even if
    // the item moved to another cell. Main-backpack rearrangement still costs time.
    const bool freeReturn = storage == sourceStorage_ && !mainBackpack;
    pending_ = PendingInventoryPlacement{inventory_.held_->item, destination, check,
        freeReturn ? 0 : sourceAccess_.seconds + access.seconds, 0, sourceAccess_, access};
    if (pending_->durationSeconds == 0) completePlacement();
    return check;
}
bool InventoryDrag::advance(double seconds) {
    if (!pending_ || !std::isfinite(seconds) || seconds < 0) return false;
    if (seconds < pending_->durationSeconds - pending_->elapsedSeconds) {
        pending_->elapsedSeconds += seconds;
        return false;
    }
    completePlacement();
    return true;
}
void InventoryDrag::completePlacement() {
    const auto placement = *pending_;
    pending_.reset();
    const auto& check = placement.result;
    const auto placed = *inventory_.held_;
    if (check.result == InventoryDrop::Swap) {
        const auto displaced = inventory_.contents_.placements.at(check.displaced);
        const auto rotation = displaced.rotation;
        sourceStorage_ = displaced.storage;
        inventory_.contents_.placements.erase(check.displaced);
        inventory_.held_ = HeldInventoryItem{check.displaced, rotation};
    } else {
        sourceStorage_.clear();
        inventory_.held_.reset();
        originalPlacements_.reset();
    }
    inventory_.contents_.placements[placed.item] = placement.destination;
    // A swap already paid for accessing the displaced item. Its next move
    // charges only destination access, even when it starts another swap.
    sourceAccess_ = {};
    inventory_.rebuild();
}
void InventoryDrag::cancel() {
    pending_.reset();
    sourceAccess_ = {};
    sourceStorage_.clear();
    if (!originalPlacements_) return;
    inventory_.contents_.placements = std::move(*originalPlacements_);
    originalPlacements_.reset();
    inventory_.held_.reset();
    inventory_.rebuild();
}
} // namespace viewer
