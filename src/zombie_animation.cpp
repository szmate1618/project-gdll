#include "zombie_animation.hpp"
#include "zombie_layer.hpp"
#include <cmath>
#include <stdexcept>

namespace viewer {
const BakedAnimationClip* zombieAnimationClip(const AnimatedModel& asset, const ZombieInstance& instance) {
    if (!instance.walking || instance.ragdoll) return nullptr;
    if (instance.running && asset.runClip) return &*asset.runClip;
    return asset.walkClip ? &*asset.walkClip : nullptr;
}

std::vector<glm::mat4> sampleZombiePose(const AnimatedModel& asset, const ZombieInstance& instance,
                                     double idleSeconds) {
    if (instance.displayedAnimationFrame >= 0) {
        const auto* displayedClip = instance.displayedAnimationClip == 2 && asset.runClip ? &*asset.runClip :
            instance.displayedAnimationClip == 1 && asset.walkClip ? &*asset.walkClip : nullptr;
        const auto& frames = displayedClip ? displayedClip->boneFrames : asset.boneFrames;
        const auto count = displayedClip ? displayedClip->frameCount : asset.frameCount;
        if (!frames.empty() && count > 0) {
            if (!std::isfinite(instance.displayedAnimationFrame) || instance.displayedAnimationFrame >= count ||
                frames.size() != count * asset.skeleton.size())
                throw std::runtime_error("Invalid published zombie animation pose");
            const auto first = static_cast<std::size_t>(instance.displayedAnimationFrame) % count;
            const auto second = (first + 1) % count;
            const float fraction = instance.displayedAnimationFrame - std::floor(instance.displayedAnimationFrame);
            std::vector<glm::mat4> pose;
            for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
                pose.push_back(frames[first * asset.skeleton.size() + bone] * (1 - fraction) +
                               frames[second * asset.skeleton.size() + bone] * fraction);
            return pose;
        }
    }
    const auto* clip = zombieAnimationClip(asset, instance);
    return sampleAnimatedPose(asset, clip ? instance.walkSeconds : idleSeconds, instance.phase, clip);
}
} // namespace viewer
