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
                                     double idleSeconds, float secondsOffset) {
    if (instance.displayedAnimationFrame >= 0) {
        const auto* displayedClip = instance.displayedAnimationClip == 2 && asset.runClip ? &*asset.runClip :
            instance.displayedAnimationClip == 1 && asset.walkClip ? &*asset.walkClip : nullptr;
        const auto& frames = displayedClip ? displayedClip->boneFrames : asset.boneFrames;
        const auto count = displayedClip ? displayedClip->frameCount : asset.frameCount;
        if (!frames.empty() && count > 0) {
            if (!std::isfinite(instance.displayedAnimationFrame) || instance.displayedAnimationFrame >= count ||
                frames.size() != count * asset.skeleton.size())
                throw std::runtime_error("Invalid published zombie animation pose");
            float frame = instance.displayedAnimationFrame;
            if (secondsOffset != 0) {
                const float duration = displayedClip ? displayedClip->duration : asset.duration;
                if (!std::isfinite(duration) || duration <= 0 || !std::isfinite(secondsOffset))
                    throw std::runtime_error("Invalid zombie animation sample offset");
                frame += secondsOffset / duration * count;
                frame -= count * std::floor(frame / count);
            }
            const auto first = static_cast<std::size_t>(frame) % count;
            const auto second = (first + 1) % count;
            const float fraction = frame - std::floor(frame);
            std::vector<glm::mat4> pose;
            for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
                pose.push_back(frames[first * asset.skeleton.size() + bone] * (1 - fraction) +
                               frames[second * asset.skeleton.size() + bone] * fraction);
            return pose;
        }
    }
    const auto* clip = zombieAnimationClip(asset, instance);
    const float duration = clip ? clip->duration : asset.duration;
    const float phaseOffset = secondsOffset != 0 && duration > 0 ? secondsOffset / duration : 0;
    const double seconds = clip ? instance.walkSeconds : instance.idleSeconds >= 0 ? instance.idleSeconds : idleSeconds;
    return sampleAnimatedPose(asset, seconds, instance.phase + phaseOffset, clip);
}
} // namespace viewer
