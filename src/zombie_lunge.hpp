#pragma once

#include "zombie_attack.hpp"

#include <glm/vec3.hpp>
#include <vector>

namespace viewer {
struct ZombieInstance;
struct ZombiePlayer;
class CollisionWorld;
class ZombieCrowd;

// Shared attack-cycle targeting/cooldown. The legacy lunge moves the root;
// zombie_melee_attack drives authored clips with the same cycle phases.
// Positions use runtime X east/Y up/Z south.
struct ZombieLunge {
    enum class Phase { ready, windup, striking, recovering };
    Phase phase = Phase::ready;
    glm::vec3 origin{0}, direction{0, 0, 1};
    float elapsed = 0, forwardTravel = 0, returnSpeed = 0;
    bool strikeStopped = false; // Hold contact pose until the forward phase ends.
    double nextAttackSeconds = 0;
    std::uint32_t targetPlayer = 0;
    bool animated = false, nextRight = false;
};

constexpr float zombieAttackWindupSeconds = .3f;
constexpr float zombieAttackRecoverySeconds = .4f;
constexpr float zombieAttackCooldownSeconds = .5f;

bool beginZombieLunge(ZombieLunge& lunge, ZombieInstance& instance,
                      const ZombiePlayer& player, double now);
void cancelZombieLunge(ZombieLunge& lunge, double now, ZombieInstance* instance = nullptr);
// All current players participate in swept body collision. Only forward
// contact can emit a hit. Recovery may stop short if its path becomes blocked.
void stepZombieLunge(ZombieLunge& lunge, ZombieInstance& instance,
                     const CollisionWorld& world, ZombieCrowd& crowd, std::size_t zombie,
                     const ZombiePlayer* players, std::size_t playerCount,
                     float seconds, double now, std::vector<ZombieAttackHit>& hits,
                     std::size_t& hitRays);
} // namespace viewer
