#include "inventory_access.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
InventoryAccess accessTime(const StorageGrid& grid, InventoryAccessScores scores) {
    double seconds = grid.accessTime;
    if (grid.backpackAccess) {
        seconds += scores.obstruction * grid.backpackAccess->obstructionTimePerCell;
        seconds -= scores.support * grid.backpackAccess->supportTimePerCell;
    }
    return {scores, std::max(0.0, seconds)};
}
} // namespace
InventoryAccess inventoryAccess(const InventoryContainer& container, const InventoryItemRectangle& rectangle,
                                const std::string& excludedItem) {
    const auto& grid = container.grid;
    if (!inventoryRectangleFits(grid, rectangle)) throw std::invalid_argument("Access footprint is outside its compartment");
    if (!grid.backpackAccess) return accessTime(grid, {});
    std::vector<bool> occupied(static_cast<std::size_t>(grid.width) * grid.height, false);
    for (const auto& other : container.items) {
        if (other.item == rectangle.item || other.item == excludedItem) continue;
        for (int y = other.y; y < other.y + other.height; ++y)
            for (int x = other.x; x < other.x + other.width; ++x)
                occupied[static_cast<std::size_t>(y) * grid.width + x] = true;
    }
    InventoryAccessScores scores;
    double supportTotal = 0;
    for (int y = rectangle.y; y < rectangle.y + rectangle.height; ++y) {
        for (int x = rectangle.x; x < rectangle.x + rectangle.width; ++x) {
            int above = 0, below = 0;
            for (int row = 0; row < y; ++row) above += occupied[static_cast<std::size_t>(row) * grid.width + x];
            for (int row = y + 1; row < grid.height; ++row) below += occupied[static_cast<std::size_t>(row) * grid.width + x];
            scores.obstruction = std::max(scores.obstruction, above);
            supportTotal += below;
        }
    }
    scores.support = supportTotal / (rectangle.width * rectangle.height);
    return accessTime(grid, scores);
}
InventoryAccess inventorySwapAccess(const InventoryContainer& container, const InventoryItemRectangle& incoming,
                                    const InventoryItemRectangle& outgoing) {
    const auto placed = inventoryAccess(container, incoming, outgoing.item);
    const auto picked = inventoryAccess(container, outgoing, incoming.item);
    return accessTime(container.grid, {std::max(placed.scores.obstruction, picked.scores.obstruction),
        std::min(placed.scores.support, picked.scores.support)});
}
} // namespace viewer
