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
