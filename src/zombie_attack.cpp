#include "zombie_attack.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
float zombieAttackDamage(glm::vec3 zombieVelocity, glm::vec3 playerVelocity) {
    const auto finite = [](glm::vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(zombieVelocity) || !finite(playerVelocity)) return 0;
    const double speed = glm::length(glm::dvec3(zombieVelocity) - glm::dvec3(playerVelocity));
    const double damage = zombieBaseAttackDamage * speed / zombieLungeSpeed;
    return static_cast<float>(std::min(damage, double(std::numeric_limits<float>::max())));
}

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
