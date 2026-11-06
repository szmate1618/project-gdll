#include "sleep_watch.hpp"
#include "collision_world.hpp"
#include "day_night.hpp"
#include "zombie_animation.hpp"
#include "zombie_layer.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
std::optional<glm::vec3> headPosition(const ZombieLayer& zombies, const ZombieInstance& instance, double seconds) {
    if (instance.asset >= zombies.assets.size()) return {};
    const auto& asset = zombies.assets[instance.asset];
    const int bone = asset.ragdollBones[std::size_t(RagdollPart::head)];
    if (bone < 0 || std::size_t(bone) >= asset.skeleton.size()) return {};
    if (instance.activeRagdoll) {
        if (std::size_t(bone) >= instance.ragdollBones.size()) return {};
        return glm::vec3(instance.transform * instance.ragdollBones[bone] *
            glm::inverse(asset.skeleton[bone].inverseBind) * glm::vec4(0,0,0,1));
    }
    // Observe the authoritative clip, never the last frame cached by rendering
    // before the screen blacked out. Only these bounded candidates sample bones.
    const auto* clip = zombieAnimationClip(asset, instance);
    const double clock = clip ? (clip->loop ? instance.walkSeconds : instance.attackSeconds) :
        instance.idleSeconds >= 0 ? instance.idleSeconds : seconds;
    const auto pose = sampleAnimatedPose(asset, clock, clip && !clip->loop ? 0 : instance.phase, clip);
    return glm::vec3(instance.transform * pose[bone] * glm::vec4(0,0,0,1));
}
}

float sleepGuardSightRange(double hour) {
    return glm::mix(100.f, 50.f, dayNightLighting(hour).nightBlend);
}
std::uint8_t sleepGuardBearing(glm::vec3 offset) {
    const double sectors = std::atan2(double(offset.x), -double(offset.z)) * 8 / std::acos(-1.);
    return static_cast<std::uint8_t>((static_cast<int>(std::floor(sectors + .5)) + 16) % 16);
}
glm::vec2 sleepGuardLookAngles(glm::vec3 eyes, glm::vec3 target) {
    const auto delta = target - eyes;
    return {glm::degrees(std::atan2(delta.z, delta.x)),
        glm::degrees(std::atan2(delta.y, glm::length(glm::vec2(delta.x, delta.z))))};
}
std::optional<SleepGuardSighting> SleepGuardWatch::update(ZombieLayer& zombies, const CollisionWorld& world,
    glm::vec3 feet, glm::vec3 eyes, double seconds, double hour) {
    if (seconds < nextScanAt_) return {};
    nextScanAt_ = seconds + sleepGuardScanSeconds;
    visibilityChecks_ = 0;
    zombies.queryLivingNear(feet, sleepGuardSightRange(hour), candidates_);
    const auto count = std::min(candidates_.size(), sleepGuardScanLimit);
    const auto distance = [&](std::size_t id) {
        const auto offset = glm::dvec3(zombies.instances[id].restRoot[3]) - glm::dvec3(feet);
        return glm::dot(offset, offset);
    };
    std::partial_sort(candidates_.begin(), candidates_.begin() + count, candidates_.end(),
        [&](std::size_t a, std::size_t b) {
            const double da = distance(a), db = distance(b);
            return da == db ? a < b : da < db;
        });
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = candidates_[i];
        const auto& instance = zombies.instances[id];
        const auto offset = feet - glm::vec3(instance.restRoot[3]);
        const glm::vec2 toward(offset.x, offset.z), velocity(instance.locomotionVelocity.x, instance.locomotionVelocity.z);
        const float speed = glm::length(velocity), range = glm::length(toward);
        // Ignore stationary actors and motion more than 45 degrees away from
        // the guard. Use solved movement, not animation facing or a route goal.
        if (speed < .1f || range < 1e-4f || glm::dot(velocity, toward) < .70710678f * speed * range) continue;
        const auto head = headPosition(zombies, instance, seconds);
        if (!head) continue;
        ++visibilityChecks_;
        if (world.lineOfSight(eyes, *head)) return SleepGuardSighting{id, *head, sleepGuardBearing(-offset)};
    }
    return {};
}
} // namespace viewer
