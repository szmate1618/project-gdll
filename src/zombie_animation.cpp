#include "zombie_animation.hpp"
#include "zombie_layer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
const BakedAnimationClip* zombieAnimationClip(const AnimatedModel& asset, const ZombieInstance& instance) {
    if (instance.ragdoll) return nullptr;
    if (instance.attackClip == 1 && asset.attackLeftClip) return &*asset.attackLeftClip;
    if (instance.attackClip == 2 && asset.attackRightClip) return &*asset.attackRightClip;
    if (!instance.walking) return nullptr;
    if (instance.running && asset.runClip) return &*asset.runClip;
    return asset.walkClip ? &*asset.walkClip : nullptr;
}

const BakedAnimationClip* zombieAnimationClipById(const AnimatedModel& asset, int id) {
    const auto* clip = id == 1 ? &asset.walkClip : id == 2 ? &asset.runClip :
        id == 3 ? &asset.attackLeftClip : id == 4 ? &asset.attackRightClip : nullptr;
    return clip && *clip ? &**clip : nullptr;
}

int zombieAnimationClipId(const AnimatedModel& asset, const ZombieInstance& instance) {
    const auto* clip = zombieAnimationClip(asset, instance);
    for (int id = 1; id <= 4; ++id)
        if (clip && clip == zombieAnimationClipById(asset, id)) return id;
    return 0;
}

std::vector<glm::mat4> sampleZombiePose(const AnimatedModel& asset, const ZombieInstance& instance,
                                     double idleSeconds, float secondsOffset) {
    if (instance.displayedAnimationFrame >= 0) {
        const auto* displayedClip = zombieAnimationClipById(asset, instance.displayedAnimationClip);
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
                if (displayedClip && !displayedClip->loop)
                    frame = std::clamp(frame + secondsOffset / duration * (count - 1), 0.f, float(count - 1));
                else {
                    frame += secondsOffset / duration * count;
                    frame -= count * std::floor(frame / count);
                }
            }
            const auto first = static_cast<std::size_t>(frame) % count;
            const auto second = displayedClip && !displayedClip->loop ? std::min(first + 1, count - 1) : (first + 1) % count;
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
    if (clip && !clip->loop) return sampleAnimatedPose(asset, instance.attackSeconds + secondsOffset, 0, clip);
    const float phaseOffset = secondsOffset != 0 && duration > 0 ? secondsOffset / duration : 0;
    const double seconds = clip ? instance.walkSeconds : instance.idleSeconds >= 0 ? instance.idleSeconds : idleSeconds;
    return sampleAnimatedPose(asset, seconds, instance.phase + phaseOffset, clip);
}
} // namespace viewer
