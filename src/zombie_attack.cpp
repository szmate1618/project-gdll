#include "zombie_attack.hpp"
#include "zombie_layer.hpp"

namespace viewer {
bool zombieInAttackRange(const ZombieInstance& instance, const ZombiePlayer& player) {
    const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
    constexpr float reach = zombieAttackDistance + .0001f;
    return glm::dot(offset, offset) <= reach * reach;
}

bool canZombieAttack(const ZombieInstance& instance, const ZombiePlayer& player,
                     const CollisionWorld& world, std::size_t& rayCount) {
    return !instance.removed && !instance.ragdoll && zombieInAttackRange(instance, player) &&
        sampleZombieSight(instance, player, world, rayCount, true) != ZombieSight::none;
}
} // namespace viewer
