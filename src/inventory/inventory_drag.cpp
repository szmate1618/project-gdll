#include "inventory_drag.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
InventoryDrag::InventoryDrag(PlayerInventory& inventory) : inventory_(inventory) {}
InventoryDrag::~InventoryDrag() { cancel(); }
bool InventoryDrag::pickUp(const std::string& item) {
    if (inventory_.held_) return false;
    const auto found = inventory_.contents_.placements.find(item);
    if (found == inventory_.contents_.placements.end()) return false;
    originalPlacements_ = inventory_.contents_.placements;
    sourcePlacement_ = found->second;
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
    if (check.result == InventoryDrop::Swap) {
        const auto& source = inventory_.contents_.owned.at(inventory_.held_->item);
        const auto& target = inventory_.contents_.owned.at(check.displaced);
        if (source.inventory().stackable && target.inventory().stackable &&
            source.inventory().ammoType == target.inventory().ammoType) {
            // A merge returns the source stack to its pickup footprint. In a
            // swap chain that footprint may already hold the preceding item.
            // Quantities can change during reloads, so they must not decide
            // whether the operation moves stack footprints or merges rounds.
            const auto sourceRectangle = inventoryRectangle(source, sourcePlacement_);
            const auto& sourceContainer = inventory_.layout_.at(sourcePlacement_.storage);
            const bool sourceFree = std::none_of(sourceContainer.items.begin(), sourceContainer.items.end(), [&](const auto& other) {
                return inventoryRectanglesOverlap(sourceRectangle, other);
            });
            if (sourceFree) check.result = InventoryDrop::Merge;
        }
    }
    return check;
}
InventoryDropCheck InventoryDrag::drop(const std::string& storage, int x, int y) {
    const auto check = preview(storage, x, y);
    if (check.result == InventoryDrop::Rejected) return check;
    const ItemPlacement destination{storage, x, y, inventory_.held_->rotation};
    const auto& container = inventory_.layout_.at(storage);
    const auto rectangle = inventoryRectangle(inventory_.contents_.owned.at(inventory_.held_->item), destination);
    const auto access = inventoryAccess(container, rectangle, check.displaced);
    const bool unchanged = check.result == InventoryDrop::Free && storage == sourcePlacement_.storage &&
        x == sourcePlacement_.x && y == sourcePlacement_.y && destination.rotation == sourcePlacement_.rotation;
    const double duration = unchanged ? 0 : inventoryTransferSeconds(&inventory_.layout_.at(sourcePlacement_.storage).grid,
        sourceAccess_.scores, &container.grid, access.scores);
    pending_ = PendingInventoryPlacement{inventory_.held_->item, destination, check,
        duration, 0, sourceAccess_, access};
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
    if (check.result == InventoryDrop::Merge) {
        const auto& target = inventory_.contents_.owned.at(check.displaced);
        const int transfer = std::min(inventory_.quantity(placed.item),
            target.inventory().maxStack - inventory_.quantity(check.displaced));
        inventory_.consume(placed.item, transfer);
        inventory_.setQuantity(check.displaced, inventory_.quantity(check.displaced) + transfer);
        inventory_.contents_.placements[placed.item] = sourcePlacement_;
        sourcePlacement_ = {};
        sourceAccess_ = {};
        inventory_.held_.reset();
        originalPlacements_.reset();
        inventory_.rebuild();
        return;
    }
    if (check.result == InventoryDrop::Swap) {
        const auto displaced = inventory_.contents_.placements.at(check.displaced);
        const auto rotation = displaced.rotation;
        sourcePlacement_ = displaced;
        // Snapshot the outgoing footprint before publishing the incoming item.
        // Automatic pickup is free now; its source access is paid on the next drop.
        sourceAccess_ = inventoryAccess(inventory_.layout_.at(sourcePlacement_.storage),
            inventoryRectangle(inventory_.contents_.owned.at(check.displaced), displaced), placed.item);
        inventory_.contents_.placements.erase(check.displaced);
        inventory_.held_ = HeldInventoryItem{check.displaced, rotation};
    } else {
        sourcePlacement_ = {};
        sourceAccess_ = {};
        inventory_.held_.reset();
        originalPlacements_.reset();
    }
    inventory_.contents_.placements[placed.item] = placement.destination;
    inventory_.rebuild();
}
void InventoryDrag::cancel() {
    pending_.reset();
    sourceAccess_ = {};
    sourcePlacement_ = {};
    if (!originalPlacements_) return;
    inventory_.contents_.placements = std::move(*originalPlacements_);
    originalPlacements_.reset();
    inventory_.held_.reset();
    inventory_.rebuild();
}
bool InventoryDrag::release() {
    if (!inventory_.held_ || pending_) return false;
    const auto id = inventory_.held_->item;
    inventory_.contents_.owned.erase(id);
    inventory_.contents_.quantities.erase(id);
    for (auto& shortcut : inventory_.contents_.hotbar) if (shortcut == id) shortcut.clear();
    inventory_.held_.reset();
    originalPlacements_.reset();
    sourceAccess_ = {};
    sourcePlacement_ = {};
    inventory_.rebuild();
    return true;
}
} // namespace viewer
