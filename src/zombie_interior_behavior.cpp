#include "zombie_layer.hpp"
#include "simulation_clock.hpp"

#include <cmath>
#include <stdexcept>

namespace viewer {

ZombieBehavior& ZombieLayer::createBehavior(std::size_t id, double now) {
    auto& behavior = behaviors.create(id, now, glm::vec3(instances.at(id).restRoot[3]));
    const auto policy = interiorBehaviors_.find(id);
    if (policy == interiorBehaviors_.end()) return behavior;
    if (policy->second.state == ZombieState::midnight && now >= policy->second.wakeSeconds) {
        interiorBehaviors_.erase(policy);
        return behavior;
    }
    behavior.state = policy->second.state;
    behavior.lookDirection = glm::vec2(0);
    return behavior;
}

void ZombieLayer::wakeInteriorBehavior(ZombieBehavior& behavior, double now) {
    if (behavior.state != ZombieState::midnight) return;
    const auto policy = interiorBehaviors_.find(behavior.zombie);
    if (policy == interiorBehaviors_.end() || now < policy->second.wakeSeconds) return;
    interiorBehaviors_.erase(policy);
    behavior.state = ZombieState::idle;
    behavior.idleSinceSeconds = now;
    behavior.perception = {}; // The first awake step observes normally.
}

void ZombieLayer::activateInteriorZombie(std::size_t id, const glm::mat4& placement,
                                        ZombieState initialState, double wakeSeconds) {
    auto& instance = instances.at(id);
    if (!instance.removed || instance.ragdoll) throw std::logic_error("Interior slot is not dormant");
    if (initialState != ZombieState::idle && initialState != ZombieState::hiding && initialState != ZombieState::midnight)
        throw std::invalid_argument("Interior spawn must start idle, hiding or waiting for midnight");
    if (std::isnan(wakeSeconds) || wakeSeconds < 0)
        throw std::invalid_argument("Interior wake time must be nonnegative simulation seconds");
    if (initialState != ZombieState::idle) interiorBehaviors_[id] = {initialState, wakeSeconds};
    else interiorBehaviors_.erase(id);
    const auto normalization = glm::inverse(instance.restRoot) * instance.restTransform;
    instance.restRoot = placement;
    instance.restTransform = instance.transform = placement * normalization;
    instance.chaseNormalization = normalization;
    setInteriorSleeping(id, false);
}

void ZombieLayer::setInteriorSleeping(std::size_t id, bool sleeping) {
    auto& instance = instances.at(id);
    if (instance.ragdoll || instance.removed == sleeping) return;
    ensureSpatialIndex();
    if (sleeping) {
        stopActiveAttack(id); behaviors.remove(id);
        houseInvestigations_.erase(id);
        instance.attackClip = 0;
        instance.attackSeconds = 0;
    }
    instance.walking = instance.running = false;
    instance.locomotionVelocity = glm::vec3(0);
    instance.removed = sleeping;
    if (!sleeping) createBehavior(id, SimulationClock::instance().seconds());
    updateSpatialMembership(id);
    markRenderChanged(id);
}

} // namespace viewer
