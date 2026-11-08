#pragma once

#include "weapon_caliber.hpp"

#include <cstddef>
#include <unordered_map>

namespace viewer {

struct ZombieInjury {
    float health = 1.f;
};

// Sparse injury records owned by one population. Keys are stable instance IDs;
// rebuilding spatial/render indexes must not reset existing injuries.
class ZombieDamage {
public:
    const ZombieInjury* injury(std::size_t zombie) const;
    // Creates the first record at full health (1.0), then applies this individual hit.
    // Returns remaining health, clamped to zero. Headshots double base damage.
    float hit(std::size_t zombie, ShotCaliber caliber, bool headshot);
    void remove(std::size_t zombie) { injuries_.erase(zombie); }

private:
    std::unordered_map<std::size_t, ZombieInjury> injuries_;
};

} // namespace viewer
