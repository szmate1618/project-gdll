#pragma once

#include "animated_model.hpp"

namespace viewer {

struct ZombieInstance;
class CollisionWorld;
class ZombieCrowd;

// Rotate in place along the shortest yaw arc at 90 degrees/s. Returns true
// when aligned. Keep the idle animation and the asset's normalization intact.
bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds);

// Desired movement in X/Z, optional crowd constraint, then terrain support.
// Returns true only on actual arrival; blocked characters retain their target.
bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& asset,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double animationSeconds,
                      ZombieCrowd* crowd = nullptr, std::size_t id = 0);

} // namespace viewer
