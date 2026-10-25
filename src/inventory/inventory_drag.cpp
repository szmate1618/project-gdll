#include "inventory_drag.hpp"

namespace viewer {
InventoryDrag::InventoryDrag(PlayerInventory& inventory) : inventory_(inventory) {}
InventoryDrag::~InventoryDrag() { cancel(); }
bool InventoryDrag::pickUp(const std::string& item) {
    if (inventory_.held_) return false;
    const auto found = inventory_.contents_.placements.find(item);
    if (found == inventory_.contents_.placements.end()) return false;
    originalPlacements_ = inventory_.contents_.placements;
    inventory_.held_ = HeldInventoryItem{item, found->second.rotation};
    inventory_.contents_.placements.erase(found);
    inventory_.rebuild();
    return true;
}
void InventoryDrag::rotate(int quarterTurns) {
    if (originalPlacements_ && inventory_.held_) inventory_.held_->rotation = ((inventory_.held_->rotation / 90 + quarterTurns % 4 + 4) % 4) * 90;
}
InventoryDropCheck InventoryDrag::preview(const std::string& storage, int x, int y) const {
    if (!originalPlacements_ || !inventory_.held_) return {};
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
    const auto placed = *inventory_.held_;
    if (check.result == InventoryDrop::Swap) {
        const auto rotation = inventory_.contents_.placements.at(check.displaced).rotation;
        inventory_.contents_.placements.erase(check.displaced);
        inventory_.held_ = HeldInventoryItem{check.displaced, rotation};
    } else {
        inventory_.held_.reset();
        originalPlacements_.reset();
    }
    inventory_.contents_.placements[placed.item] = {storage, x, y, placed.rotation};
    inventory_.rebuild();
    return check;
}
void InventoryDrag::cancel() {
    if (!originalPlacements_) return;
    inventory_.contents_.placements = std::move(*originalPlacements_);
    originalPlacements_.reset();
    inventory_.held_.reset();
    inventory_.rebuild();
}
} // namespace viewer
