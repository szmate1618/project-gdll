#pragma once

#include "weapons.hpp"
#include "inventory/player_inventory.hpp"

namespace viewer {
struct WeaponSwitchTiming {
    double putAway = 0, storageAccess = 0, draw = 0; // Seconds.
    double total() const { return putAway + storageAccess + draw; }
};
// Storage locations remain reserved while equipped. Score both footprints
// excluding the pair; charge each compartment base once and both adjustments.
std::optional<WeaponSwitchTiming> weaponSwitchTiming(const PlayerInventory& inventory,
    const WeaponConfig& oldWeapon, const WeaponConfig& newWeapon);

// Simulation-thread transaction. Call update before consuming combat input.
// Direct Weapons::equip remains available for initialization/replication.
class WeaponSwitch {
public:
    bool begin(Weapons& weapons, const PlayerInventory& inventory, std::size_t target, double seconds);
    bool update(Weapons& weapons, double seconds); // True only on a completed switch.
    bool pending() const { return pending_.has_value(); }
    double remaining(double seconds) const;
    const WeaponSwitchTiming* timing() const { return pending_ ? &pending_->timing : nullptr; }
private:
    struct Pending {
        std::size_t from, to;
        double readyAt;
        WeaponSwitchTiming timing;
    };
    std::optional<Pending> pending_;
};
} // namespace viewer
