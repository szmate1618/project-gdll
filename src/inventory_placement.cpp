#include "inventory_placement.hpp"
#include "item_config.hpp"

#include <stdexcept>

namespace viewer {
std::string bodyStorageReference(const std::string& equipment, const std::string& slot) {
    return "body/" + equipment + "/" + slot;
}
std::string backpackStorageReference(const std::string& backpack, const std::string& slot) {
    return "backpack/" + backpack + "/" + slot;
}

InventoryLayout buildInventoryLayout(const StorageLoadout& loadout, const InventoryItems& inventory) {
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
        const auto invalid = [&](const std::string& reason) {
            throw std::runtime_error("inventory.toml: placements." + id + " " + reason);
        };
        const auto placement = inventory.placements.find(id);
        if (placement == inventory.placements.end()) invalid("is required for every owned item");
        const auto& entry = placement->second;
        const auto found = result.find(entry.storage);
        if (found == result.end()) invalid("references unavailable storage " + entry.storage);
        auto& container = found->second;
        const auto& grid = container.grid;
        if (item.width < 1 || item.height < 1 || item.width > grid.width || item.height > grid.height ||
            entry.x < 0 || entry.y < 0 || entry.x > grid.width - item.width || entry.y > grid.height - item.height)
            invalid("extends outside " + entry.storage);
        for (const auto& placed : container.items)
            if (entry.x < placed.x + placed.width && entry.x + item.width > placed.x &&
                entry.y < placed.y + placed.height && entry.y + item.height > placed.y)
                invalid("overlaps " + placed.item + " in " + entry.storage);
        container.items.push_back({id, entry.x, entry.y, item.width, item.height});
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
