#pragma once

#include "weapons.hpp"
#include <optional>
#include <string>

namespace viewer {
class PlayerInventory;
struct WeaponReloadPlan {
    std::string source;
    int rounds = 0;
    double duration = 0;
};
// Read-only eligibility/selection shared with immediate client sound prediction.
std::optional<WeaponReloadPlan> planWeaponReload(const WeaponState& weapon, const PlayerInventory& inventory);

// One reload uses one stored ammo stack. Its source, transfer quantity and
// access time are selected when the operation starts; ammo moves on completion.
class WeaponReload {
public:
    bool begin(Weapons& weapons, const PlayerInventory& inventory, double seconds);
    int update(Weapons& weapons, PlayerInventory& inventory, double seconds);
    bool pending() const { return pending_.has_value(); }
    double remaining(double seconds) const;
    double duration() const { return pending_ ? pending_->duration : 0; }
private:
    struct Pending {
        std::size_t slot;
        std::string source;
        int rounds;
        double readyAt, duration;
    };
    std::optional<Pending> pending_;
};
} // namespace viewer
