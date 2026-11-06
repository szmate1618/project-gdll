#pragma once

#include "zombie_lunge.hpp"

namespace viewer {
struct AnimatedModel;

constexpr float zombieHandRadius = .10f; // World meters, on the attacking wrist/hand joint.

// The existing attack cycle owns targeting/cooldown. Authored attacks keep the
// root stationary. Mapped rigs drive joint springs from the non-looping clip;
// custom rigs without physical mappings use its baked hand path.
bool hasZombieMeleeAttack(const AnimatedModel& asset);
bool beginZombieMeleeAttack(ZombieLunge& cycle, ZombieInstance& instance,
                           const AnimatedModel& asset, const ZombiePlayer& player, double now);
// Snapshot a movement contact point in world meters. Uses the same authored
// clips/cooldown, without replacing the remembered pursuit/investigation target.
bool beginZombieDoorAttack(ZombieLunge& cycle, ZombieInstance& instance,
                          const AnimatedModel& asset, glm::vec3 contact, double now);
void stepZombieMeleeAttack(ZombieLunge& cycle, ZombieInstance& instance,
                          const AnimatedModel& asset, const CollisionWorld& world, std::size_t zombie,
                          const ZombiePlayer* players, std::size_t playerCount, float seconds, double now,
                          std::vector<ZombieAttackHit>& hits, std::size_t& hitRays,
                          std::vector<NoiseEvent>* noises = nullptr);
} // namespace viewer
