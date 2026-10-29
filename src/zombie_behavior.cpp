#include "zombie_update_profile.hpp"
#include "zombie_behavior.hpp"
#include "simulation_clock.hpp"
#include "noise.hpp"
#include "zombie_chase.hpp"
#include "zombie_attack.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {
constexpr float activationDistance = 100.0f;
constexpr float suspensionDistance = activationDistance * 1.5f;
constexpr float approachDistance = zombieAttackApproachDistance;
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
    if (!playerVisible || std::abs(dy) >= approachDistance) return target;
    const glm::vec2 offset(target.x - feet.x, target.z - feet.z);
    const float distance = glm::length(offset);
    if (distance > 1e-4f) {
        const auto inset = offset / distance * std::sqrt(approachDistance * approachDistance - dy * dy);
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

bool stepBehavior(ZombieBehavior& behavior, ZombieInstance& instance, const AnimatedModel& asset,
                  ZombiePlayer player, const CollisionWorld& world,
                  float seconds, double now, double animationSeconds,
                  bool staggerPerception, ZombieBehaviorStats& stats, ZombieCrowd& crowd,
                  const std::vector<ZombiePlayer>* players,
                  bool scenarioFallback, std::vector<ZombieAttackHit>& hits) {
    if (behavior.state == ZombieState::looking && turnZombieToward(instance, behavior.lookDirection, seconds))
        changeState(behavior, ZombieState::idle, now);
    const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
    const float squaredDistance = glm::dot(offset, offset);
    float scheduleDistance = squaredDistance;
    if (players) for (const auto& p : *players) {
        const auto d = p.feet - glm::vec3(instance.restRoot[3]);
        scheduleDistance = std::min(scheduleDistance, glm::dot(d, d));
    }
    updatePerceptionRange(behavior, scheduleDistance);
    const bool observed = !behavior.perceptionSuspended &&
        (!staggerPerception || behavior.perception.due(now, scheduleDistance, behavior.zombie));
    if (observed) {
        ++stats.perceptionChecks;
        // Scheduled observations retain none/partial/full for later attack
        // transitions. Every-step chasing only needs the first clear ray.
        if (players && behavior.lunge.phase == ZombieLunge::Phase::ready) {
            // Keep a visible target; otherwise select the nearest visible player.
            ZombieSight bestSight = ZombieSight::none;
            float bestDistance = std::numeric_limits<float>::max();
            for (const auto& candidate : *players) {
                const auto delta = candidate.feet - glm::vec3(instance.restRoot[3]);
                const float distance = glm::dot(delta, delta);
                if (distance > suspensionDistance * suspensionDistance) continue;
                const auto sight = sampleZombieSight(instance, candidate, world, stats.sightRays);
                if (sight == ZombieSight::none) continue;
                if (candidate.id == behavior.targetPlayer) { player = candidate; bestSight = sight; break; }
                if (distance < bestDistance) { player = candidate; bestSight = sight; bestDistance = distance; }
            }
            if (behavior.targetPlayer != player.id) { behavior.attackClock = {}; behavior.attackVisible = false; }
            behavior.targetPlayer = player.id;
            behavior.sight = bestSight;
        } else behavior.sight = sampleZombieSight(instance, player, world, stats.sightRays,
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
            if (zombie_profile::activeProfile) ++zombie_profile::activeProfile->chaseStarts;
        } else if (behavior.state == ZombieState::idle || behavior.state == ZombieState::looking)
            changeState(behavior, ZombieState::investigating, now);
    }
    if (behavior.state == ZombieState::idle || behavior.state == ZombieState::looking) {
        // An idle observation does not own a movement target. Keep an initial
        // scenario route resident until perception supplies a gameplay goal.
        if (scenarioFallback) return false;
        stopZombieLocomotion(instance);
        return false;
    }

    const auto closeEnough = [&] {
        const auto delta = player.feet - glm::vec3(instance.restRoot[3]);
        constexpr float reach = approachDistance + .0001f;
        return glm::dot(delta, delta) <= reach * reach;
    };
    const auto stepAttack = [&] {
        stepZombieLunge(behavior.lunge, instance, world, crowd, behavior.zombie,
            players ? players->data() : &player, players ? players->size() : 1,
            seconds, now, hits, stats.attackRays);
    };
    auto feet = glm::vec3(instance.restRoot[3]);
    const bool eligible = attackEligible(behavior, instance, player, world, now, stats);
    if (behavior.lunge.phase != ZombieLunge::Phase::ready) {
        if (behavior.lunge.phase == ZombieLunge::Phase::windup && !eligible) {
            cancelZombieLunge(behavior.lunge, now);
        } else {
            stepAttack();
            return true; // A committed strike/recovery does not track a dodging player.
        }
    }
    if (eligible && closeEnough()) {
        changeState(behavior, ZombieState::attacking, now);
        stopZombieLocomotion(instance);
        if (beginZombieLunge(behavior.lunge, instance, player, now)) stepAttack();
        return true;
    }
    if (behavior.state == ZombieState::attacking)
        changeState(behavior, behavior.sight == ZombieSight::full ? ZombieState::chasing : ZombieState::investigating, now);

    const float speed = behavior.state == ZombieState::chasing ? runSpeed : walkSpeed;
    const auto destination = approachDestination(behavior.target, feet, behavior.sight != ZombieSight::none);
    const bool arrived = moveZombieToward(instance, asset, destination, speed, seconds, world, animationSeconds,
                                         &crowd, behavior.zombie);
    feet = glm::vec3(instance.restRoot[3]);
    if (attackEligible(behavior, instance, player, world, now, stats) && closeEnough()) {
        changeState(behavior, ZombieState::attacking, now);
        beginZombieLunge(behavior.lunge, instance, player, now);
    } else if (arrived && (behavior.sight != ZombieSight::none ||
             glm::length(glm::vec2(behavior.target.x - feet.x, behavior.target.z - feet.z)) <= 1e-4f))
        changeState(behavior, ZombieState::idle, now);
    if (behavior.state == ZombieState::idle || behavior.state == ZombieState::attacking) {
        stopZombieLocomotion(instance);
    }
    return true;
}

}

ZombieBehavior& ZombieBehaviorPool::create(std::size_t zombie, double now, glm::vec3 feet) {
    if (auto* existing = find(zombie)) return *existing;
    if (zombie == std::numeric_limits<std::size_t>::max())
        throw std::length_error("Zombie behavior ID exceeds spatial-index capacity");
    hearingIndex_.growPopulation(zombie + 1);
    entries_.push_back({zombie, ZombieState::idle, now, now, glm::vec3(0)});
    try {
        indices_.emplace(zombie, entries_.size() - 1);
        hearingIndex_.update(zombie, feet);
    } catch (...) {
        hearingIndex_.erase(zombie);
        indices_.erase(zombie);
        entries_.pop_back();
        throw;
    }
    return entries_.back();
}

bool ZombieBehaviorPool::remove(std::size_t zombie) {
    const auto found = indices_.find(zombie);
    if (found == indices_.end()) return false;
    hearingIndex_.erase(zombie);
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

void ZombieBehaviorPool::query(glm::vec3 center, double radius, std::vector<std::size_t>& candidates) const {
    hearingIndex_.query(center, radius, candidates);
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

void ZombieLayer::setPerceptionEnabled(bool enabled) {
    if (perceptionEnabled_ == enabled) return;
    perceptionEnabled_ = enabled;
    behaviorStats_ = {};
    attackHits_.clear();
    if (enabled) return;
    for (const auto& entry : behaviors.entries()) {
        if (entry.zombie >= instances.size()) continue;
        auto& instance = instances[entry.zombie];
        if (instance.removed || instance.ragdoll) continue;
        if (instance.walking) markRenderChanged(entry.zombie, false);
        stopZombieLocomotion(instance);
    }
    behaviors = ZombieBehaviorPool{};
    behaviorOrder_.clear();
}

void ZombieLayer::recordReaction(std::size_t id, double now, bool chase) {
    if (reactionEvents_.size() == 256) reactionEvents_.pop_front();
    reactionEvents_.push_back({id, glm::vec3(instances[id].restRoot[3]) + glm::vec3(0, 1.5f, 0), now, chase});
}
std::vector<ZombieReactionEvent> ZombieLayer::takeReactionEvents() {
    std::vector<ZombieReactionEvent> result(reactionEvents_.begin(), reactionEvents_.end());
    reactionEvents_.clear();
    return result;
}

void ZombieLayer::makeNoise(glm::vec3 position, float decibels) {
    if (!perceptionEnabled_ || !finite(position) || !std::isfinite(decibels)) return;
    const double now = SimulationClock::instance().seconds();
    if (decibels < zombieHearingThresholdDecibels) return;
    // Expand by one threshold ULP: noiseLevelAt returns float and may round a
    // value just below the threshold up. The original level test remains final.
    const float threshold = std::nextafter(zombieHearingThresholdDecibels, -std::numeric_limits<float>::infinity());
    const double radius = std::pow(10.0, (static_cast<double>(decibels) - threshold) / 20.0);
    behaviors.query(position, radius, spatialCandidates_);
    behaviorStats_.hearingCandidates += spatialCandidates_.size();
    for (const auto id : spatialCandidates_) {
        auto* behavior = behaviors.find(id);
        if (!behavior || behavior->state != ZombieState::idle || id >= instances.size()) continue;
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
        recordReaction(id, now, false);
        if (instance.walking) markRenderChanged(id, false);
        stopZombieLocomotion(instance);
    }
}

void ZombieLayer::updateBehavior(float seconds, const ZombiePlayer& player,
                                const CollisionWorld& world, double animationSeconds) {
    updateBehaviorImpl(seconds, player, world, animationSeconds, nullptr);
}
void ZombieLayer::updateBehaviorForPlayers(float seconds, const std::vector<ZombiePlayer>& players,
                                         const CollisionWorld& world, double animationSeconds) {
    updateBehaviorImpl(seconds, players.empty() ? ZombiePlayer{} : players.front(), world, animationSeconds, &players);
}
void ZombieLayer::updateBehaviorImpl(float seconds, const ZombiePlayer& player,
                                    const CollisionWorld& world, double animationSeconds,
                                    const std::vector<ZombiePlayer>* players) {
    zombie_profile::Timer timer(zombie_profile::Stage::behaviorDispatch);
    zombie_profile::PhaseTimer phase(&zombie_profile::Profile::behaviorSeconds);
    attackHits_.clear();
    if (!std::isfinite(seconds) || seconds <= 0) return;
    if (!perceptionEnabled_) {
        behaviorStats_ = {};
        if (!scenarioRoutes_.empty()) {
            ensureSpatialIndex();
            crowd_.beginStep();
            phase.finish();
            updateScenarioRoutes(seconds, world, player, players);
        }
        return; // No nearby-player queries or behavior creation in this mode.
    }
    if (!finite(player.feet) || !finite(player.velocity) ||
        !std::isfinite(player.eyeHeight) || !std::isfinite(player.height) ||
        !std::isfinite(player.radius) || player.radius <= 0 ||
        player.eyeHeight <= 0 || player.height < player.eyeHeight) return;
    if (players) {
        for (const auto& p : *players)
            if (!finite(p.feet) || !finite(p.velocity) || !std::isfinite(p.radius) || p.radius <= 0 ||
                !std::isfinite(p.eyeHeight) || !std::isfinite(p.height) ||
                p.eyeHeight <= 0 || p.height < p.eyeHeight) return;
    }
    const double now = SimulationClock::instance().seconds();
    behaviorStats_ = {};
    ensureSpatialIndex();
    crowd_.beginStep();
    if (players && players->empty()) {
        for (const auto& entry : behaviors.entries()) {
            auto& behavior = *behaviors.find(entry.zombie);
            if (behavior.lunge.phase == ZombieLunge::Phase::ready) continue;
            cancelZombieLunge(behavior.lunge, now);
            if (entry.zombie < instances.size()) stopZombieLocomotion(instances[entry.zombie]);
            changeState(behavior, ZombieState::idle, now);
            behavior.attackVisible = false;
            behavior.attackClock = {};
        }
        phase.finish();
        updateScenarioRoutes(seconds, world, player, players, true);
        return;
    }
    // The small expansion keeps the broad phase conservative around float
    // distance rounding. Activation still uses the original exact predicate.
    spatialIndex_.query(player.feet, activationDistance + 0.0001, spatialCandidates_);
    if (players) {
        std::vector<std::size_t> nearby;
        for (const auto& p : *players) {
            spatialIndex_.query(p.feet, activationDistance + .0001, nearby);
            spatialCandidates_.insert(spatialCandidates_.end(), nearby.begin(), nearby.end());
        }
        std::sort(spatialCandidates_.begin(), spatialCandidates_.end());
        spatialCandidates_.erase(std::unique(spatialCandidates_.begin(), spatialCandidates_.end()), spatialCandidates_.end());
    }
    behaviorStats_.activationCandidates = spatialCandidates_.size();
    for (const auto id : spatialCandidates_) {
        auto& instance = instances[id];
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size() || behaviors.contains(id)) continue;
        const auto close = [&](const ZombiePlayer& p) {
            const auto offset = p.feet - glm::vec3(instance.restRoot[3]);
            return glm::dot(offset, offset) <= activationDistance * activationDistance;
        };
        if (players ? std::any_of(players->begin(), players->end(), close) : close(player)) {
            behaviors.create(id, now, glm::vec3(instance.restRoot[3]));
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
        const bool scenarioFallback = scenarioDestination(id) && !scenarioFinished(id);
        const bool wasWalking = id < instances.size() && instances[id].walking;
        if (id >= instances.size() || instances[id].removed || instances[id].ragdoll || instances[id].asset >= assets.size() ||
            (behavior.state == ZombieState::idle && now - behavior.idleSinceSeconds >= idleLifetime)) {
            if (!scenarioFallback) {
                if (id < instances.size()) stopZombieLocomotion(instances[id]);
                if (wasWalking) markRenderChanged(id, false);
            }
            behaviors.remove(id);
            continue;
        }
        auto& instance = instances[id];
        auto selectedPlayer = player;
        auto rangePlayer = player;
        if (players) {
            float nearest = std::numeric_limits<float>::max();
            bool retained = false;
            for (const auto& p : *players) {
                const auto d = p.feet - glm::vec3(instance.restRoot[3]);
                if (glm::dot(d, d) < nearest) { nearest = glm::dot(d, d); rangePlayer = p; }
                const auto target = behavior.lunge.phase == ZombieLunge::Phase::ready ?
                    behavior.targetPlayer : behavior.lunge.targetPlayer;
                if (p.id == target) { selectedPlayer = p; retained = true; }
            }
            if (!retained) {
                selectedPlayer = rangePlayer;
                behavior.perception = {};
                behavior.attackClock = {};
                behavior.sight = ZombieSight::none;
                if (behavior.lunge.phase == ZombieLunge::Phase::windup)
                    cancelZombieLunge(behavior.lunge, now);
            }
        }
        if (distantIdle(behavior, instance, rangePlayer)) {
            if (!scenarioFallback) {
                stopZombieLocomotion(instance);
                if (wasWalking) markRenderChanged(id, false);
            }
            behaviors.remove(id);
            continue;
        }
        const auto beforeTransform = instance.transform;
        const auto beforeRoot = instance.restRoot;
        const auto beforeSeconds = instance.walkSeconds;
        const bool wasRunning = instance.running;
        const auto previousChase = instance.chaseRevision;
        const bool claimed = stepBehavior(behavior, instance, assets[instance.asset], selectedPlayer, world,
                     seconds, now, animationSeconds, staggerPerception_, behaviorStats_, crowd_, players,
                     scenarioFallback, attackHits_);
        if (instance.chaseRevision != previousChase) recordReaction(id, now, true);
        if (zombie_profile::activeProfile)
            ++zombie_profile::activeProfile->behaviorStates[std::size_t(behavior.state)];
        if (claimed && scenarioDestination(id)) abandonScenarioRoute(id);
        const bool moved = beforeTransform != instance.transform;
        if (moved || wasWalking != instance.walking || wasRunning != instance.running || beforeSeconds != instance.walkSeconds)
            markRenderChanged(id, moved);
        if(moved || beforeRoot!=instance.restRoot || instance.removed || instance.ragdoll || instance.asset>=assets.size())
            updateSpatialMembership(id);
        // Arrival or a completed looking turn can become idle in this step.
        // Keep the living zombie indexed for later activation and shooting.
        if (distantIdle(behavior, instance, rangePlayer)) {
            behaviors.remove(id);
            continue;
        }
    }
    phase.finish();
    updateScenarioRoutes(seconds, world, player, players);
}

} // namespace viewer
