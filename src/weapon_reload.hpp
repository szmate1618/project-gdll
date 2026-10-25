#pragma once

#include "weapons.hpp"
#include <optional>
#include <string>

namespace viewer {
class PlayerInventory;

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
