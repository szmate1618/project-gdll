#pragma once

#include "animated_model.hpp"

namespace viewer {

struct ZombieInstance;
class CollisionWorld;

// Rotate in place along the shortest yaw arc at 90 degrees/s. Returns true
// when aligned. Keep the idle animation and the asset's normalization intact.
bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds);

// Straight-line movement in X/Z with terrain support; returns true on arrival.
bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& asset,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double animationSeconds);

// Add a simple one-second leg cycle to the idle pose. Bones rotate around
// asset-space X (Y up, +Z forward); mapped descendants inherit each swing.
std::vector<glm::mat4> sampleZombieWalkPose(const AnimatedModel& asset,
                                         double animationSeconds, double walkSeconds,
                                         float phase);

} // namespace viewer
