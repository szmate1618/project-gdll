#include "zombie_animation.hpp"
#include "zombie_layer.hpp"

namespace viewer {
const BakedAnimationClip* zombieAnimationClip(const AnimatedModel& asset, const ZombieInstance& instance) {
    if (!instance.walking || instance.ragdoll) return nullptr;
    if (instance.running && asset.runClip) return &*asset.runClip;
    return asset.walkClip ? &*asset.walkClip : nullptr;
}

std::vector<glm::mat4> sampleZombiePose(const AnimatedModel& asset, const ZombieInstance& instance,
                                     double idleSeconds) {
    const auto* clip = zombieAnimationClip(asset, instance);
    return sampleAnimatedPose(asset, clip ? instance.walkSeconds : idleSeconds, instance.phase, clip);
}
} // namespace viewer
