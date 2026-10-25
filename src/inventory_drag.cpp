#include "inventory_drag.hpp"

namespace viewer {
InventoryDrag::InventoryDrag(const StorageLoadout& storage, InventoryItems& inventory)
    : storage_(storage), inventory_(inventory), layout_(buildInventoryLayout(storage, inventory)) {}
void InventoryDrag::rebuild() {
    layout_ = buildInventoryLayout(storage_, inventory_, held_ ? held_->item : "");
}
bool InventoryDrag::pickUp(const std::string& item) {
    if (held_) return false;
    const auto found = inventory_.placements.find(item);
    if (found == inventory_.placements.end()) return false;
    originalPlacements_ = inventory_.placements;
    held_ = HeldInventoryItem{item, found->second.rotation};
    inventory_.placements.erase(found);
    rebuild();
    return true;
}
void InventoryDrag::rotate(int quarterTurns) {
    if (held_) held_->rotation = ((held_->rotation / 90 + quarterTurns % 4 + 4) % 4) * 90;
}
InventoryDropCheck InventoryDrag::preview(const std::string& storage, int x, int y) const {
    if (!held_) return {};
    const auto found = layout_.find(storage);
    if (found == layout_.end()) return {};
    if (!inventoryItemAllowed(found->second.grid, inventory_.owned.at(held_->item))) return {};
    const auto rectangle = inventoryRectangle(inventory_.owned.at(held_->item), {storage, x, y, held_->rotation});
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
    const auto placed = *held_;
    if (check.result == InventoryDrop::Swap) {
        const auto rotation = inventory_.placements.at(check.displaced).rotation;
        inventory_.placements.erase(check.displaced);
        held_ = HeldInventoryItem{check.displaced, rotation};
    } else {
        held_.reset();
        originalPlacements_.reset();
    }
    inventory_.placements[placed.item] = {storage, x, y, placed.rotation};
    rebuild();
    return check;
}
void InventoryDrag::cancel() {
    if (!originalPlacements_) return;
    inventory_.placements = std::move(*originalPlacements_);
    originalPlacements_.reset();
    held_.reset();
    rebuild();
}
} // namespace viewer
