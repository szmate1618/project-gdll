#include "weapon_switch.hpp"
#include "inventory/inventory_access.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
std::optional<WeaponSwitchTiming> weaponSwitchTiming(const PlayerInventory& inventory,
    const WeaponConfig& oldWeapon, const WeaponConfig& newWeapon) {
    if (oldWeapon.id == newWeapon.id) return WeaponSwitchTiming{};
    struct Access { const StorageGrid* grid = nullptr; InventoryAccessScores scores; };
    const auto access = [&](const WeaponConfig& weapon, const std::string& partner) -> std::optional<Access> {
        if (weapon.id == "punch") return Access{};
        const auto location = inventory.locationOf(weapon.id);
        if (!location || !location->placement) return {};
        const auto& container = inventory.layout().at(location->placement->storage);
        const auto rectangle = inventoryRectangle(*inventory.find(weapon.id), *location->placement);
        return Access{&container.grid, inventoryAccess(container, rectangle, partner).scores};
    };
    auto oldAccess = access(oldWeapon, newWeapon.id), newAccess = access(newWeapon, oldWeapon.id);
    if (!oldAccess || !newAccess) return {};
    if (oldAccess->scores.obstruction >= newAccess->scores.obstruction) newAccess->scores.obstruction = 0;
    else oldAccess->scores.obstruction = 0;
    return WeaponSwitchTiming{oldWeapon.drawTimeSeconds,
        oldAccess->grid ? inventoryAccessSeconds(*oldAccess->grid, oldAccess->scores) : 0,
        newAccess->grid ? inventoryAccessSeconds(*newAccess->grid, newAccess->scores) : 0,
        newWeapon.drawTimeSeconds};
}
bool WeaponSwitch::begin(Weapons& weapons, const PlayerInventory& inventory, std::size_t target, double seconds) {
    if (pending_ || target > punchSlot || target == weapons.slot() || !std::isfinite(seconds) ||
        !weapons.state(target).owned) return false;
    const auto timing = weaponSwitchTiming(inventory, weapons.equipped().config, weapons.state(target).config);
    if (!timing || !std::isfinite(seconds + timing->total())) return false;
    pending_ = Pending{weapons.slot(), target, seconds + timing->total(), *timing};
    weapons.cancelTrigger();
    return true;
}
bool WeaponSwitch::update(Weapons& weapons, double seconds) {
    if (!pending_) return false;
    if (weapons.slot() != pending_->from || !weapons.equipped().owned || !weapons.state(pending_->to).owned) {
        pending_.reset(); return false;
    }
    weapons.cancelTrigger(); // Held clicks during the transition cannot fire at completion.
    if (!std::isfinite(seconds) || seconds < pending_->readyAt) return false;
    const auto target = pending_->to;
    pending_.reset();
    return weapons.equip(target);
}
double WeaponSwitch::remaining(double seconds) const {
    return pending_ ? std::max(0.0, pending_->readyAt - seconds) : 0;
}
} // namespace viewer
