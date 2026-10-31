#pragma once

#include "animated_model.hpp"

namespace viewer {
struct ZombieInstance;

// Ordinary movement uses shared baked vertices. Skeletons are sampled only
// for hits/handoff or temporary active-ragdoll attack targets. A published
// camera frame takes precedence over continuous simulation time, including LOD
// animation stepping and the last displayed locomotion clip.
// Physical attackers return their solved skeleton. secondsOffset otherwise
// samples relative to the displayed pose; attack endpoints clamp rather than loop.
const BakedAnimationClip* zombieAnimationClip(const AnimatedModel& asset, const ZombieInstance& instance);
const BakedAnimationClip* zombieAnimationClipById(const AnimatedModel& asset, int id);
int zombieAnimationClipId(const AnimatedModel& asset, const ZombieInstance& instance);
std::vector<glm::mat4> sampleZombiePose(const AnimatedModel& asset, const ZombieInstance& instance,
                                     double idleSeconds, float secondsOffset = 0);
} // namespace viewer
