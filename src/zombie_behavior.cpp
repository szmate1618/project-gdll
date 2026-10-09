#include "zombie_behavior.hpp"
#include "simulation_clock.hpp"
#include "zombie_chase.hpp"
#include "zombie_layer.hpp"

#include <cmath>
#include <utility>

namespace viewer {
namespace {
constexpr float activationDistance = 100.0f;
constexpr float attackDistance = 1.5f;
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

int visiblePlayerPoints(const ZombieInstance& instance, const ZombiePlayer& player,
                        const CollisionWorld& world) {
    const auto feet = glm::vec3(instance.restRoot[3]);
    if (!zombiePlayerInFov(feet, glm::vec3(instance.restRoot[2]), player.feet)) return 0;
    const auto eyes = feet + glm::vec3(0, 1.65f, 0);
    int visible = 0;
    for (const float height : {player.eyeHeight, player.height * 0.5f, 0.0f})
        visible += world.lineOfSight(eyes, player.feet + glm::vec3(0, height, 0));
    return visible;
}

glm::vec3 approachDestination(glm::vec3 target, glm::vec3 feet,
                              const ZombiePlayer& player, bool playerVisible) {
    // Stop at attack range while approaching a currently seen player, even
    // if a caller supplies a large simulation step. Targets remain last seen
    // feet; this offset only affects this step's locomotion destination.
    const float dy = player.feet.y - feet.y;
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

void stepBehavior(ZombieBehavior& behavior, ZombieInstance& instance, const AnimatedModel& asset,
                  const ZombiePlayer& player, const CollisionWorld& world,
                  float seconds, double now, double animationSeconds) {
    const int visible = visiblePlayerPoints(instance, player, world);
    if (visible > 0) {
        behavior.target = player.feet;
        if (visible == 3 && (behavior.state == ZombieState::idle || behavior.state == ZombieState::investigating))
            changeState(behavior, ZombieState::chasing, now);
        else if (behavior.state == ZombieState::idle)
            changeState(behavior, ZombieState::investigating, now);
    }
    if (behavior.state == ZombieState::idle) return;

    auto feet = glm::vec3(instance.restRoot[3]);
    if (glm::length(player.feet - feet) <= attackDistance + 1e-4f) {
        changeState(behavior, ZombieState::attacking, now);
        return;
    }
    if (behavior.state == ZombieState::attacking)
        changeState(behavior, visible == 3 ? ZombieState::chasing : ZombieState::investigating, now);

    const float speed = behavior.state == ZombieState::chasing ? runSpeed : walkSpeed;
    const auto destination = approachDestination(behavior.target, feet, player, visible > 0);
    const bool arrived = moveZombieToward(instance, asset, destination, speed, seconds, world, animationSeconds);
    feet = glm::vec3(instance.restRoot[3]);
    if (glm::length(player.feet - feet) <= attackDistance + 1e-4f)
        changeState(behavior, ZombieState::attacking, now);
    else if (arrived)
        changeState(behavior, ZombieState::idle, now);
    if (behavior.state == ZombieState::idle || behavior.state == ZombieState::attacking)
        instance.walking = false;
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

void ZombieLayer::updateBehavior(float seconds, const ZombiePlayer& player,
                                const CollisionWorld& world, double animationSeconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || !finite(player.feet) ||
        !std::isfinite(player.eyeHeight) || !std::isfinite(player.height) ||
        player.eyeHeight <= 0 || player.height < player.eyeHeight) return;
    const double now = SimulationClock::instance().seconds();
    // Only the cheap distance/membership pass touches the entire crowd.
    // Perception and locomotion run exclusively on the dense behavior pool.
    for (std::size_t id = 0; id < instances.size(); ++id) {
        auto& instance = instances[id];
        instance.walking = false;
        if (instance.ragdoll || instance.asset >= assets.size() || behaviors.contains(id)) continue;
        const auto offset = player.feet - glm::vec3(instance.restRoot[3]);
        if (glm::dot(offset, offset) <= activationDistance * activationDistance) {
            behaviors.create(id, now);
            instance.chaseNormalization = glm::inverse(instance.restRoot) * instance.restTransform;
        }
    }

    // Creation precedes expiration: a removed idle member stays absent for
    // this step and can be recreated in idle on the next nearby-player step.
    std::size_t index = 0;
    while (index < behaviors.entries().size()) {
        const auto id = behaviors.entries()[index].zombie;
        auto& behavior = *behaviors.find(id);
        if (id >= instances.size() || instances[id].ragdoll || instances[id].asset >= assets.size() ||
            (behavior.state == ZombieState::idle && now - behavior.idleSinceSeconds >= idleLifetime)) {
            behaviors.remove(id);
            continue; // Process the member moved into this slot.
        }
        auto& instance = instances[id];
        stepBehavior(behavior, instance, assets[instance.asset], player, world,
                     seconds, now, animationSeconds);
        ++index;
    }
}

} // namespace viewer
