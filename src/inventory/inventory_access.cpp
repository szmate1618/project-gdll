#include "inventory_access.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
double adjustmentSeconds(const StorageGrid& grid, InventoryAccessScores scores) {
    if (!grid.backpackAccess) return 0;
    return scores.obstruction * grid.backpackAccess->obstructionTimePerCell
        - scores.support * grid.backpackAccess->supportTimePerCell;
}
}
double inventoryAccessSeconds(const StorageGrid& grid, InventoryAccessScores scores) {
    return std::max(0.0, grid.accessTime + adjustmentSeconds(grid, scores));
}
double inventoryTransferSeconds(const StorageGrid* source, InventoryAccessScores sourceScores,
                                const StorageGrid* destination, InventoryAccessScores destinationScores) {
    double seconds = 0;
    if (source) seconds += source->accessTime + adjustmentSeconds(*source, sourceScores);
    if (destination) {
        if (destination != source) seconds += destination->accessTime;
        seconds += adjustmentSeconds(*destination, destinationScores);
    }
    return std::max(0.0, seconds);
}
InventoryAccessRange inventoryAccessRange(const StorageGrid& grid, int itemHeight) {
    if (itemHeight < 1 || itemHeight > grid.height) throw std::invalid_argument("Invalid access footprint height");
    const int otherRows = grid.height - itemHeight;
    return {inventoryAccessSeconds(grid, {0, static_cast<double>(otherRows)}),
            inventoryAccessSeconds(grid, {otherRows, 0})};
}
InventoryAccess inventoryAccess(const InventoryContainer& container, const InventoryItemRectangle& rectangle,
                                const std::string& excludedItem) {
    const auto& grid = container.grid;
    if (!inventoryRectangleFits(grid, rectangle)) throw std::invalid_argument("Access footprint is outside its compartment");
    if (!grid.backpackAccess) return {{}, inventoryAccessSeconds(grid, {})};
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
    return {scores, inventoryAccessSeconds(grid, scores)};
}
} // namespace viewer
