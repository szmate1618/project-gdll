#pragma once

#include <cstddef>

namespace viewer {
struct ZombieInstance;
struct ZombiePlayer;
class CollisionWorld;

constexpr float zombieAttackDistance = 1.5f; // World-space feet distance, meters.
bool zombieInAttackRange(const ZombieInstance& instance, const ZombiePlayer& player);
// Uncached range/FOV/visibility check. Any head, mid-body or feet ray suffices.
// Future damage application must call this at the hit, never trust state/cache.
// Counts only rays actually cast, and exits on the first clear ray.
bool canZombieAttack(const ZombieInstance& instance, const ZombiePlayer& player,
                     const CollisionWorld& world, std::size_t& rayCount);
} // namespace viewer
