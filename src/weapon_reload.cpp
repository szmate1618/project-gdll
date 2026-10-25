#include "weapon_reload.hpp"
#include "inventory/inventory_access.hpp"
#include "inventory/player_inventory.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
bool WeaponReload::begin(Weapons& weapons, const PlayerInventory& inventory, double seconds) {
    if (pending_ || weapons.reloading_ || !std::isfinite(seconds)) return false;
    const auto& weapon = weapons.equipped();
    const int missing = weapon.config.magazineCapacity - weapon.loaded;
    if (!weapon.owned || missing <= 0) return false;
    struct Source { std::string id; int quantity; double access; };
    std::optional<Source> best;
    for (const auto& [id, item] : inventory.items()) {
        if (item.inventory().type != "ammo" || item.inventory().ammoType != weapon.config.ammoType) continue;
        const int quantity = inventory.quantity(id);
        const auto location = inventory.locationOf(id);
        if (quantity <= 0 || !location || !location->placement) continue;
        const auto& placement = *location->placement;
        const auto& container = inventory.layout().at(placement.storage);
        const double access = inventoryAccess(container, inventoryRectangle(item, placement)).seconds;
        if (!std::isfinite(access)) continue;
        const bool enough = quantity >= missing;
        const bool bestEnough = best && best->quantity >= missing;
        if (!best || access < best->access || (access == best->access &&
            ((enough && !bestEnough) || (enough == bestEnough &&
                (enough ? quantity < best->quantity : quantity > best->quantity)))))
            best = Source{id, quantity, access};
    }
    if (!best) return false;
    const double duration = weapon.config.reloadTimeSeconds + best->access;
    if (!std::isfinite(seconds + duration)) return false;
    pending_ = Pending{weapons.slot(), best->id, std::min(missing, best->quantity), seconds + duration, duration};
    weapons.reloading_ = true;
    weapons.cancelTrigger();
    return true;
}

int WeaponReload::update(Weapons& weapons, PlayerInventory& inventory, double seconds) {
    if (!pending_) return 0;
    weapons.cancelTrigger(); // Clicks held during loading cannot fire at completion.
    if (!std::isfinite(seconds) || seconds < pending_->readyAt) return 0;
    const auto operation = *pending_;
    pending_.reset();
    weapons.reloading_ = false;
    if (weapons.slot() != operation.slot || !weapons.equipped().owned) return 0;
    auto& weapon = weapons.weapons_[operation.slot];
    const auto* source = inventory.find(operation.source);
    if (!source || source->inventory().type != "ammo" || source->inventory().ammoType != weapon.config.ammoType) return 0;
    const int rounds = std::min({operation.rounds, inventory.quantity(operation.source),
        weapon.config.magazineCapacity - weapon.loaded});
    if (rounds <= 0 || !inventory.consume(operation.source, rounds)) return 0;
    weapon.loaded += rounds;
    weapons.syncAmmo(inventory);
    return rounds;
}

double WeaponReload::remaining(double seconds) const {
    return pending_ ? std::max(0.0, pending_->readyAt - seconds) : 0;
}
} // namespace viewer
