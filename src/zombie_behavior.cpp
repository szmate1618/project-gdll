#include "zombie_behavior.hpp"
#include "simulation_clock.hpp"
#include "noise.hpp"
#include "zombie_chase.hpp"
#include "zombie_attack.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace viewer {
namespace {
constexpr float activationDistance = 100.0f;
constexpr float suspensionDistance = activationDistance * 1.5f;
constexpr float attackDistance = zombieAttackDistance;
constexpr double attackCheckPeriod = .1;
constexpr float walkSpeed = 1.2f;
constexpr float runSpeed = 3.0f;
constexpr double idleLifetime = 10.0;

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void changeState(ZombieBehavior& behavior, ZombieState state, double now) {
    if (behavior.state == state) return;
    behavior.state = state;
    if (state == ZombieState::idle) behavior.idleSinceSeconds = now;
}

void updatePerceptionRange(ZombieBehavior& behavior, float squaredDistance) {
    if (squaredDistance > suspensionDistance * suspensionDistance) {
        behavior.perceptionSuspended = true;
        // A cached clear view must not shorten the approach to the last target.
        // Suspension loses visibility, not target memory or pursuit state.
        behavior.sight = ZombieSight::none;
    } else if (behavior.perceptionSuspended && squaredDistance <= activationDistance * activationDistance) {
        behavior.perceptionSuspended = false;
        behavior.perception = {}; // Observe immediately on reentry, then stagger normally.
    }
}

bool distantIdle(const ZombieBehavior& behavior, const ZombieInstance& instance, const ZombiePlayer& player) {
    if (behavior.state != ZombieState::idle) return false;
    const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
    return glm::dot(offset, offset) > suspensionDistance * suspensionDistance;
}

glm::vec3 approachDestination(glm::vec3 target, glm::vec3 feet, bool playerVisible) {
    // Stop short of the last observed target while sight is cached as clear.
    // Use its recorded height too, without tracking an unsampled player jump.
    const float dy = target.y - feet.y;
    if (!playerVisible || std::abs(dy) >= attackDistance) return target;
    const glm::vec2 offset(target.x - feet.x, target.z - feet.z);
    const float distance = glm::length(offset);
    if (distance > 1e-4f) {
        const auto inset = offset / distance * std::sqrt(attackDistance * attackDistance - dy * dy);
        target.x -= inset.x;
        target.z -= inset.y;
    }
    return target;
}

bool attackEligible(ZombieBehavior& behavior, const ZombieInstance& instance, const ZombiePlayer& player,
                    const CollisionWorld& world, double now, ZombieBehaviorStats& stats) {
    if (!zombieInAttackRange(instance, player)) {
        behavior.attackVisible = false;
        behavior.attackClock = {}; // Reentry needs an immediate fresh observation.
        return false;
    }
    if (behavior.attackClock.dueEvery(now, attackCheckPeriod, behavior.zombie)) {
        ++stats.attackChecks;
        behavior.attackVisible = canZombieAttack(instance, player, world, stats.attackRays);
        // A fresh failed close-range observation supersedes old clear sight.
        // Preserve the remembered target, but do not stop short of a player
        // whom a door now hides just because normal perception isn't due yet.
        if (!behavior.attackVisible) behavior.sight = ZombieSight::none;
    }
    return behavior.attackVisible;
}

void stepBehavior(ZombieBehavior& behavior, ZombieInstance& instance, const AnimatedModel& asset,
                  const ZombiePlayer& player, const CollisionWorld& world,
                  float seconds, double now, double animationSeconds,
                  bool staggerPerception, ZombieBehaviorStats& stats, ZombieCrowd& crowd) {
    if (behavior.state == ZombieState::looking && turnZombieToward(instance, behavior.lookDirection, seconds))
        changeState(behavior, ZombieState::idle, now);
    const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
    const float squaredDistance = glm::dot(offset, offset);
    updatePerceptionRange(behavior, squaredDistance);
    const bool observed = !behavior.perceptionSuspended &&
        (!staggerPerception || behavior.perception.due(now, squaredDistance, behavior.zombie));
    if (observed) {
        ++stats.perceptionChecks;
        // Scheduled observations retain none/partial/full for later attack
        // transitions. Every-step chasing only needs the first clear ray.
        behavior.sight = sampleZombieSight(instance, player, world, stats.sightRays,
                                           !staggerPerception && behavior.state == ZombieState::chasing);
    }
    const bool visible = !behavior.perceptionSuspended && behavior.sight != ZombieSight::none;
    if (observed && visible) {
        behavior.target = player.feet;
        if (behavior.sight == ZombieSight::full &&
            (behavior.state == ZombieState::idle || behavior.state == ZombieState::looking ||
             behavior.state == ZombieState::investigating)) {
            changeState(behavior, ZombieState::chasing, now);
            ++instance.chaseRevision;
            instance.chaseSeconds = now;
        } else if (behavior.state == ZombieState::idle || behavior.state == ZombieState::looking)
            changeState(behavior, ZombieState::investigating, now);
    }
    if (behavior.state == ZombieState::idle || behavior.state == ZombieState::looking) {
        stopZombieLocomotion(instance);
        return;
    }

    auto feet = glm::vec3(instance.restRoot[3]);
    if (attackEligible(behavior, instance, player, world, now, stats)) {
        changeState(behavior, ZombieState::attacking, now);
        stopZombieLocomotion(instance);
        return;
    }
    if (behavior.state == ZombieState::attacking)
        changeState(behavior, behavior.sight == ZombieSight::full ? ZombieState::chasing : ZombieState::investigating, now);

    const float speed = behavior.state == ZombieState::chasing ? runSpeed : walkSpeed;
    const auto destination = approachDestination(behavior.target, feet, behavior.sight != ZombieSight::none);
    const bool arrived = moveZombieToward(instance, asset, destination, speed, seconds, world, animationSeconds,
                                         &crowd, behavior.zombie);
    feet = glm::vec3(instance.restRoot[3]);
    if (attackEligible(behavior, instance, player, world, now, stats))
        changeState(behavior, ZombieState::attacking, now);
    else if (arrived && (behavior.sight != ZombieSight::none ||
             glm::length(glm::vec2(behavior.target.x - feet.x, behavior.target.z - feet.z)) <= 1e-4f))
        changeState(behavior, ZombieState::idle, now);
    if (behavior.state == ZombieState::idle || behavior.state == ZombieState::attacking)
        stopZombieLocomotion(instance);
}

}

ZombieBehavior& ZombieBehaviorPool::create(std::size_t zombie, double now) {
    if (auto* existing = find(zombie)) return *existing;
    entries_.push_back({zombie, ZombieState::idle, now, now, glm::vec3(0)});
    try {
        indices_.emplace(zombie, entries_.size() - 1);
    } catch (...) {
        entries_.pop_back();
        throw;
    }
    return entries_.back();
}

bool ZombieBehaviorPool::remove(std::size_t zombie) {
    const auto found = indices_.find(zombie);
    if (found == indices_.end()) return false;
    const auto index = found->second;
    if (index != entries_.size() - 1) {
        entries_[index] = std::move(entries_.back());
        indices_.at(entries_[index].zombie) = index;
    }
    entries_.pop_back();
    indices_.erase(found);
    return true;
}

bool ZombieBehaviorPool::contains(std::size_t zombie) const {
    return indices_.find(zombie) != indices_.end();
}

ZombieBehavior* ZombieBehaviorPool::find(std::size_t zombie) {
    const auto found = indices_.find(zombie);
    return found == indices_.end() ? nullptr : &entries_[found->second];
}

const ZombieBehavior* ZombieBehaviorPool::find(std::size_t zombie) const {
    const auto found = indices_.find(zombie);
    return found == indices_.end() ? nullptr : &entries_[found->second];
}

bool zombiePlayerInFov(glm::vec3 feet, glm::vec3 forward, glm::vec3 playerFeet) {
    if (!finite(feet) || !finite(forward) || !finite(playerFeet)) return false;
    const glm::vec2 offset(playerFeet.x - feet.x, playerFeet.z - feet.z);
    const glm::vec2 facing(forward.x, forward.z);
    const float squaredDistance = glm::dot(offset, offset);
    if (squaredDistance < 1e-8f) return true;
    const float squaredFacing = glm::dot(facing, facing);
    if (squaredFacing < 1e-8f) return false;
    // cos(50 degrees): a total 100-degree horizontal cone, including edges.
    return glm::dot(offset, facing) >= 0.64278761f * std::sqrt(squaredDistance * squaredFacing) - 1e-5f;
}

void ZombieLayer::chase(float seconds, glm::vec3 target, const CollisionWorld& world,
                       double animationSeconds) {
    updateBehavior(seconds, ZombiePlayer{target}, world, animationSeconds);
}

void ZombieLayer::setPerceptionStaggering(bool enabled) {
    staggerPerception_ = enabled;
    for (const auto& entry : behaviors.entries()) behaviors.find(entry.zombie)->perception = {};
}

void ZombieLayer::makeNoise(glm::vec3 position, float decibels) {
    if (!finite(position) || !std::isfinite(decibels)) return;
    const double now = SimulationClock::instance().seconds();
    if (decibels < zombieHearingThresholdDecibels) return;
    ensureSpatialIndex();
    // Expand by one threshold ULP: noiseLevelAt returns float and may round a
    // value just below the threshold up. The original level test remains final.
    const float threshold = std::nextafter(zombieHearingThresholdDecibels, -std::numeric_limits<float>::infinity());
    const double radius = std::pow(10.0, (static_cast<double>(decibels) - threshold) / 20.0);
    spatialIndex_.query(position, radius, spatialCandidates_);
    behaviorStats_.hearingCandidates += spatialCandidates_.size();
    for (const auto id : spatialCandidates_) {
        auto* behavior = behaviors.find(id);
        if (!behavior || behavior->state != ZombieState::idle) continue;
        auto& instance = instances[id];
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size()) continue;
        const auto feet = glm::vec3(instance.restRoot[3]);
        if (noiseLevelAt({position, decibels}, feet) < zombieHearingThresholdDecibels) continue;
        const glm::vec2 offset(position.x - feet.x, position.z - feet.z);
        const float distance = glm::length(offset);
        if (!std::isfinite(distance) || distance < 1e-4f) continue; // No usable horizontal direction.
        behavior->lookDirection = offset / distance;
        changeState(*behavior, ZombieState::looking, now);
        ++instance.heardNoiseRevision;
        instance.heardNoiseSeconds = now;
        if (instance.walking) markRenderChanged(id, false);
        stopZombieLocomotion(instance);
    }
}

void ZombieLayer::updateBehavior(float seconds, const ZombiePlayer& player,
                                const CollisionWorld& world, double animationSeconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || !finite(player.feet) ||
        !std::isfinite(player.eyeHeight) || !std::isfinite(player.height) ||
        player.eyeHeight <= 0 || player.height < player.eyeHeight) return;
    const double now = SimulationClock::instance().seconds();
    behaviorStats_ = {};
    ensureSpatialIndex();
    crowd_.beginStep();
    // The small expansion keeps the broad phase conservative around float
    // distance rounding. Activation still uses the original exact predicate.
    spatialIndex_.query(player.feet, activationDistance + 0.0001, spatialCandidates_);
    behaviorStats_.activationCandidates = spatialCandidates_.size();
    for (const auto id : spatialCandidates_) {
        auto& instance = instances[id];
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size() || behaviors.contains(id)) continue;
        const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
        if (glm::dot(offset, offset) <= activationDistance * activationDistance) {
            behaviors.create(id, now);
            instance.chaseNormalization = glm::inverse(instance.restRoot) * instance.restTransform;
        }
    }

    // Creation precedes expiration: a removed idle member stays absent for
    // this step and can be recreated in idle on the next nearby-player step.
    // Pool swap removal must not change collision priority. Reuse the scratch
    // storage and sort only active IDs, never scan the full city population.
    behaviorOrder_.clear();
    for (const auto& entry : behaviors.entries()) behaviorOrder_.push_back(entry.zombie);
    std::sort(behaviorOrder_.begin(), behaviorOrder_.end());
    for (const auto id : behaviorOrder_) {
        auto& behavior = *behaviors.find(id);
        const bool wasWalking = id < instances.size() && instances[id].walking;
        if (id >= instances.size() || instances[id].removed || instances[id].ragdoll || instances[id].asset >= assets.size() ||
            (behavior.state == ZombieState::idle && now - behavior.idleSinceSeconds >= idleLifetime)) {
            if (id < instances.size()) stopZombieLocomotion(instances[id]);
            if (wasWalking) markRenderChanged(id, false);
            behaviors.remove(id);
            continue;
        }
        auto& instance = instances[id];
        if (distantIdle(behavior, instance, player)) {
            stopZombieLocomotion(instance);
            if (wasWalking) markRenderChanged(id, false);
            behaviors.remove(id);
            continue;
        }
        const auto beforeTransform = instance.transform;
        const auto beforeSeconds = instance.walkSeconds;
        const bool wasRunning = instance.running;
        stepBehavior(behavior, instance, assets[instance.asset], player, world,
                     seconds, now, animationSeconds, staggerPerception_, behaviorStats_, crowd_);
        const bool moved = beforeTransform != instance.transform;
        if (moved || wasWalking != instance.walking || wasRunning != instance.running || beforeSeconds != instance.walkSeconds)
            markRenderChanged(id, moved);
        updateSpatialMembership(id);
        // Arrival or a completed looking turn can become idle in this step.
        // Keep the living zombie indexed for later activation and shooting.
        if (distantIdle(behavior, instance, player)) {
            behaviors.remove(id);
            continue;
        }
    }
}

} // namespace viewer
