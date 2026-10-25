#pragma once

#include "inventory_placement.hpp"

namespace viewer {
struct InventoryAccessScores {
    int obstruction = 0;
    double support = 0;
};
struct InventoryAccess {
    InventoryAccessScores scores;
    double seconds = 0;
};
// Applies the configured base, obstruction and support terms, floored at zero.
double inventoryAccessSeconds(const StorageGrid& grid, InventoryAccessScores scores);
struct InventoryAccessRange { double best = 0, worst = 0; };
// Theoretical attainable limits for a footprint of this height. Width does not
// change column scores. Default is the full range for a one-cell item.
InventoryAccessRange inventoryAccessRange(const StorageGrid& grid, int itemHeight = 1);

// Counts occupied cells along each footprint cell's vertical column. The
// queried item and optional exchange partner are excluded, including cells
// outside the proposed footprint. Empty gaps do not stop the scan.
// Fixed-access compartments return zero scores and their configured base time.
InventoryAccess inventoryAccess(const InventoryContainer& container, const InventoryItemRectangle& rectangle,
                                const std::string& excludedItem = {});

// One destination access for placing the incoming item and auto-picking the
// outgoing item: max obstruction, min support, with both items excluded.
InventoryAccess inventorySwapAccess(const InventoryContainer& container, const InventoryItemRectangle& incoming,
                                    const InventoryItemRectangle& outgoing);
} // namespace viewer
