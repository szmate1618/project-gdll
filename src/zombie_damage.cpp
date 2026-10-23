#include "zombie_damage.hpp"

#include <algorithm>

namespace viewer {

const ZombieInjury* ZombieDamage::injury(std::size_t zombie) const {
    const auto found = injuries_.find(zombie);
    return found == injuries_.end() ? nullptr : &found->second;
}

int ZombieDamage::hit(std::size_t zombie, ShotCaliber caliber, bool headshot) {
    auto& injury = injuries_.try_emplace(zombie).first->second;
    const int damage = (caliber == ShotCaliber::high ? 2 : 1) * (headshot ? 2 : 1);
    injury.health = std::max(0, injury.health - damage);
    return injury.health;
}

} // namespace viewer
