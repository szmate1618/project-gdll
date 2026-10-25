#include "inventory_placement.hpp"
#include "player_inventory.hpp"

#include <algorithm>
#include <stdexcept>

namespace viewer {
std::string bodyStorageReference(const std::string& equipment, const std::string& slot) {
    return "body/" + equipment + "/" + slot;
}
std::string backpackStorageReference(const std::string& backpack, const std::string& slot) {
    return "backpack/" + backpack + "/" + slot;
}

InventoryItemRectangle inventoryRectangle(const Item& item, const ItemPlacement& placement) {
    if (placement.rotation < 0 || placement.rotation > 270 || placement.rotation % 90 != 0)
        throw std::runtime_error("Invalid inventory rotation for " + item.id());
    const bool rotated = placement.rotation % 180 != 0;
    return {item.id(), placement.x, placement.y, rotated ? item.inventory().height : item.inventory().width, rotated ? item.inventory().width : item.inventory().height};
}
bool inventoryRectangleFits(const StorageGrid& grid, const InventoryItemRectangle& rectangle) {
    return rectangle.width > 0 && rectangle.height > 0 && rectangle.width <= grid.width && rectangle.height <= grid.height &&
        rectangle.x >= 0 && rectangle.y >= 0 && rectangle.x <= grid.width - rectangle.width &&
        rectangle.y <= grid.height - rectangle.height;
}
bool inventoryItemAllowed(const StorageGrid& grid, const Item& item) {
    return grid.allowedTags.empty() || std::any_of(grid.allowedTags.begin(), grid.allowedTags.end(), [&](const auto& tag) {
        return item.hasTag(tag);
    });
}
bool inventoryRectanglesOverlap(const InventoryItemRectangle& a, const InventoryItemRectangle& b) {
    return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

InventoryLayout buildInventoryLayout(const StorageLoadout& loadout, const InventoryContents& inventory, const std::string& heldItem) {
    if (!heldItem.empty() && (inventory.owned.find(heldItem) == inventory.owned.end() || inventory.placements.count(heldItem)))
        throw std::runtime_error("Held inventory item must be owned and have no placement");
    InventoryLayout result;
    const auto add = [&](const std::string& reference, const StorageGrid& grid) {
        if (!result.emplace(reference, InventoryContainer{grid, {}}).second)
            throw std::runtime_error("Duplicate storage reference: " + reference);
    };
    for (const auto& equipment : loadout.equipment)
        for (const auto& slot : equipment.slots)
            add(bodyStorageReference(equipment.id, slot.id), slot.grid);
    if (loadout.backpack) {
        const auto& backpack = *loadout.backpack;
        add(backpackStorageReference(backpack.id), backpack.grid);
        for (const auto& slot : backpack.slots)
            add(backpackStorageReference(backpack.id, slot.id), slot.grid);
    }
    for (const auto& [id, item] : inventory.owned) {
        if (id == heldItem) continue;
        const auto invalid = [&](const std::string& reason) {
            throw std::runtime_error("inventory.toml: placements." + id + " " + reason);
        };
        if (id != item.id()) invalid("does not match its item definition ID");
        const auto placement = inventory.placements.find(id);
        if (placement == inventory.placements.end()) invalid("is required for every owned item");
        const auto& entry = placement->second;
        const auto found = result.find(entry.storage);
        if (found == result.end()) invalid("references unavailable storage " + entry.storage);
        auto& container = found->second;
        if (!inventoryItemAllowed(container.grid, item)) invalid("has no allowed tag for " + entry.storage);
        const auto rectangle = inventoryRectangle(item, entry);
        if (!inventoryRectangleFits(container.grid, rectangle))
            invalid("extends outside " + entry.storage);
        for (const auto& placed : container.items)
            if (inventoryRectanglesOverlap(rectangle, placed))
                invalid("overlaps " + placed.item + " in " + entry.storage);
        container.items.push_back(rectangle);
    }
    // Also reject stale placements when callers construct a snapshot directly.
    for (const auto& [id, placement] : inventory.placements) {
        (void)placement;
        if (inventory.owned.find(id) == inventory.owned.end())
            throw std::runtime_error("inventory.toml: placements." + id + " is not owned");
    }
    return result;
}
} // namespace viewer
