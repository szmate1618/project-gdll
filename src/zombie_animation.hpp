#pragma once

#include "animated_model.hpp"

namespace viewer {
struct ZombieInstance;

// Ordinary movement uses shared baked vertices. Skeletons are sampled only
// when a hit test or ragdoll handoff needs the exact displayed pose. A published
// camera frame takes precedence over continuous simulation time, including LOD
// animation stepping and the last displayed locomotion clip.
const BakedAnimationClip* zombieAnimationClip(const AnimatedModel& asset, const ZombieInstance& instance);
std::vector<glm::mat4> sampleZombiePose(const AnimatedModel& asset, const ZombieInstance& instance,
                                     double idleSeconds);
} // namespace viewer
