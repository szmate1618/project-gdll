#include "zombie_melee_attack.hpp"
#include "zombie_layer.hpp"
#include "zombie_chase.hpp"
#include "zombie_hit_capsules.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
int handBone(const AnimatedModel& asset, bool right) {
    const char* name = right ? "bip R Hand" : "bip L Hand";
    for (std::size_t i = 0; i < asset.skeleton.size(); ++i)
        if (asset.skeleton[i].name == name) return static_cast<int>(i);
    return -1;
}

glm::vec3 handPosition(const AnimatedModel& asset, const ZombieInstance& instance,
                       const BakedAnimationClip& clip, int bone, double seconds) {
    const float frame = clampedAnimationFrame(clip.duration, clip.frameCount, seconds);
    const auto first = static_cast<std::size_t>(frame);
    const auto second = std::min(first + 1, clip.frameCount - 1);
    const auto count = asset.skeleton.size();
    // Baked bone translations use the exact vertex-animation sampling times.
    // Only this hand is sampled; attacks never evaluate the whole crowd rig.
    const auto point = glm::mix(clip.boneFrames.at(first * count + std::size_t(bone))[3],
        clip.boneFrames.at(second * count + std::size_t(bone))[3], frame - std::floor(frame));
    return glm::vec3(instance.transform * point);
}

bool blocked(const CollisionWorld& world, glm::vec3 start, glm::vec3 end) {
    if (!world.lineOfSight(start, end)) return true;
    return !world.contacts(end - glm::vec3(0, zombieHandRadius, 0),
        zombieHandRadius, 2 * zombieHandRadius).empty();
}

float contact(glm::vec3 start, glm::vec3 end, const ZombiePlayer& player) {
    ZombieBoneCapsule capsule;
    capsule.bone = 0;
    const float radius = std::min(player.radius, player.height * .5f);
    capsule.a = {0, radius, 0}; capsule.b = {0, player.height - radius, 0};
    capsule.radius = radius + zombieHandRadius;
    static const std::vector<glm::mat4> identity{glm::mat4(1)};
    const auto movement = end - start;
    const auto direction = glm::dot(movement, movement) > 1e-12f ? movement : glm::vec3(0, 1e-6f, 0);
    float fraction = 0;
    return zombieCapsuleRay(start - player.feet, direction, glm::mat4(1), capsule, identity, fraction) &&
        fraction <= 1 ? fraction : std::numeric_limits<float>::infinity();
}
}

bool hasZombieMeleeAttack(const AnimatedModel& asset) {
    return asset.attackLeftClip && asset.attackRightClip && handBone(asset, false) >= 0 && handBone(asset, true) >= 0;
}

bool beginZombieMeleeAttack(ZombieLunge& cycle, ZombieInstance& instance,
                           const AnimatedModel& asset, const ZombiePlayer& player, double now) {
    if (!hasZombieMeleeAttack(asset) || cycle.phase != ZombieLunge::Phase::ready || now < cycle.nextAttackSeconds)
        return false;
    const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
    const auto direction = glm::vec2(offset.x, offset.z);
    if (glm::length(direction) < 1e-5f) return false;
    turnZombieToward(instance, glm::normalize(direction), 2.f);
    instance.attackClip = cycle.nextRight ? 2 : 1;
    instance.attackSeconds = 0;
    instance.running = false;
    const auto& clip = cycle.nextRight ? *asset.attackRightClip : *asset.attackLeftClip;
    cycle.nextRight = !cycle.nextRight;
    cycle.animated = true;
    cycle.phase = ZombieLunge::Phase::windup;
    cycle.elapsed = 0;
    cycle.strikeStopped = false;
    cycle.targetPlayer = player.id;
    cycle.nextAttackSeconds = now + clip.duration + zombieAttackCooldownSeconds;
    return true;
}

void stepZombieMeleeAttack(ZombieLunge& cycle, ZombieInstance& instance,
                          const AnimatedModel& asset, const CollisionWorld& world, std::size_t zombie,
                          const ZombiePlayer* players, std::size_t playerCount, float seconds, double now,
                          std::vector<ZombieAttackHit>& hits, std::size_t& hitRays) {
    if (cycle.phase == ZombieLunge::Phase::ready) return;
    if (instance.removed || instance.ragdoll || !instance.attackClip) {
        cancelZombieLunge(cycle, now, &instance); return;
    }
    const bool right = instance.attackClip == 2;
    const auto& clip = right ? *asset.attackRightClip : *asset.attackLeftClip;
    const int bone = handBone(asset, right);
    const double before = instance.attackSeconds;
    const double after = std::min(before + seconds, double(clip.duration));
    // The pack winds up before ~0.6 s and swings fastest around 0.8--1.0 s.
    // Only that forward swing can injure; idle, windup and retraction cannot.
    const double strikeStart = clip.duration * .25, strikeEnd = clip.duration * .55;
    double time = std::max(before, strikeStart);
    const double end = std::min(after, strikeEnd);
    auto hand = handPosition(asset, instance, clip, bone, time);
    while (!cycle.strikeStopped && time < end) {
        const double next = std::min(end, time + 1.0 / 120);
        const auto desired = handPosition(asset, instance, clip, bone, next);
        // Trace from the supported torso too: a hand already behind a newly
        // closed door must not injure someone through that door.
        const auto chest = glm::vec3(instance.restRoot[3]) + glm::vec3(0, 1.2f, 0);
        if (blocked(world, hand, desired) || !world.lineOfSight(chest, desired)) {
            cycle.strikeStopped = true; break;
        }
        float nearest = std::numeric_limits<float>::infinity();
        const ZombiePlayer* victim = nullptr;
        for (std::size_t p = 0; p < playerCount; ++p) {
            const float fraction = contact(hand, desired, players[p]);
            if (fraction < nearest || (fraction == nearest && victim && players[p].id < victim->id)) {
                nearest = fraction; victim = &players[p];
            }
        }
        if (victim) {
            if (canZombieAttack(instance, *victim, world, hitRays))
                hits.push_back({zombie, victim->id, zombieBaseAttackDamage});
            cycle.strikeStopped = true; // One physical contact consumes the swing.
        }
        hand = desired; time = next;
    }
    instance.attackSeconds = after;
    stopZombieLocomotion(instance);
    cycle.phase = after < strikeStart ? ZombieLunge::Phase::windup :
        after < strikeEnd ? ZombieLunge::Phase::striking : ZombieLunge::Phase::recovering;
    if (after >= clip.duration) cancelZombieLunge(cycle, now, &instance);
}
} // namespace viewer
