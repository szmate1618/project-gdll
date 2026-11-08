#pragma once

#include "player_health.hpp"
#include "inventory/player_inventory.hpp"

namespace viewer {
// Authority applies a dressing to exactly one still-bleeding wound. A stale
// request consumes nothing. Storage transactions must be idle at the caller.
inline bool applyBandage(PlayerHealth& health, PlayerInventory& inventory,
                         const std::string& itemId, std::uint64_t injury) {
    const auto* item = inventory.find(itemId);
    if (!item || item->inventory().type != "medical" || !item->hasTag("bandage") ||
        inventory.held() || inventory.quantity(itemId) <= 0 || !health.bandage(injury)) return false;
    return inventory.consume(itemId, 1);
}
} // namespace viewer
