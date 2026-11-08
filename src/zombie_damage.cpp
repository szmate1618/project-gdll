#include "zombie_damage.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

const ZombieInjury* ZombieDamage::injury(std::size_t zombie) const {
    const auto found = injuries_.find(zombie);
    return found == injuries_.end() ? nullptr : &found->second;
}

float ZombieDamage::hit(std::size_t zombie, ShotCaliber caliber, bool headshot) {
    auto& injury = injuries_.try_emplace(zombie).first->second;
    const float damage = (caliber == ShotCaliber::high ? .2f : .1f) * (headshot ? 2 : 1);
    injury.health = std::max(0.f, std::round((injury.health - damage) * 10.f) / 10.f);
    if (injury.health < 1e-6f) injury.health = 0;
    return injury.health;
}

} // namespace viewer
