#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>

namespace viewer {
struct ZombieInstance;
struct ZombiePlayer;
class CollisionWorld;

constexpr float zombieAttackDistance = 1.5f; // World-space feet distance, meters.
constexpr float zombieAttackApproachDistance = 1.0f;
constexpr float zombieLungeDistance = 1.5f;
constexpr float zombieLungeSeconds = .15f;
constexpr float zombieLungeSpeed = zombieLungeDistance / zombieLungeSeconds; // m/s before impact.
constexpr float zombieBaseAttackDamage = 10.0f;

struct ZombieAttackHit {
    std::size_t zombie = 0;
    std::uint32_t player = 0;
    float damage = 0;
};

// Relative velocity magnitude, normalized to an unobstructed lunge against a
// stationary player. Both input velocities are world-space meters/second.
float zombieAttackDamage(glm::vec3 zombieVelocity, glm::vec3 playerVelocity);
bool zombieInAttackRange(const ZombieInstance& instance, const ZombiePlayer& player);
// Uncached range/FOV/visibility check. Any head, mid-body or feet ray suffices.
// Future damage application must call this at the hit, never trust state/cache.
// Counts only rays actually cast, and exits on the first clear ray.
bool canZombieAttack(const ZombieInstance& instance, const ZombiePlayer& player,
                     const CollisionWorld& world, std::size_t& rayCount);
} // namespace viewer
