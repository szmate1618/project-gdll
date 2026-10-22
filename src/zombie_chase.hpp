#pragma once

#include "animated_model.hpp"

namespace viewer {

struct ZombieInstance;
class CollisionWorld;
class ZombieCrowd;
struct ZombieMovement;

// Explicit idle/attack/looking transitions stop immediately, bypassing the
// short animation hold used for intermittent locomotion obstruction.
void stopZombieLocomotion(ZombieInstance& instance);

// Rotate in place along the shortest yaw arc at 90 degrees/s. Returns true
// when aligned. Keep the idle animation and the asset's normalization intact.
bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds);

// Apply an already accepted physical movement to facing and locomotion clips.
// Shared by direct behavior movement and scenario routes; no navigation policy.
void applyZombieLocomotion(ZombieInstance& instance, glm::vec2 desiredVelocity,
                           const ZombieMovement& movement, float speed, float seconds);

// Original direct movement in X/Z, constrained by scene, crowd and terrain.
// Returns true only on actual arrival; blocked characters retain their target.
// Small contact slides retain facing. Locomotion turns at most 180 degrees/s,
// with a short gait hold/debounce while collision progress is intermittent.
bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& asset,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double animationSeconds,
                      ZombieCrowd* crowd = nullptr, std::size_t id = 0);

} // namespace viewer
