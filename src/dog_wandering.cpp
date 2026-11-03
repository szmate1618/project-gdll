#include "dog_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
constexpr float pi = 3.14159265359f;
constexpr float arrivalDistance = .01f;
float horizontalDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(glm::vec2(a.x-b.x, a.z-b.z));
}
void finish(DogBehavior& behavior, double now) {
    behavior.state = DogState::idle;
    behavior.idleSince = now;
    behavior.wander = {};
}
} // namespace

std::optional<glm::vec3> dogWanderTarget(glm::vec3 feet, glm::vec2 direction,
                                      float distance, const CollisionWorld& world) {
    for (const float degrees : {0.f, 30.f, -30.f, 45.f, -45.f}) {
        const float angle = degrees*pi/180;
        // Positive angles turn +Z toward +X, matching the runtime yaw frame.
        const glm::vec2 rotated(direction.x*std::cos(angle)+direction.y*std::sin(angle),
                                direction.y*std::cos(angle)-direction.x*std::sin(angle));
        auto target = feet+glm::vec3(rotated.x*distance, 0, rotated.y*distance);
        if (!world.empty()) {
            auto ground = world.groundAt(target.x, target.z,
                std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), true);
            if (!ground) ground = world.walkingGroundAt(target.x, target.z,
                std::numeric_limits<float>::lowest(), feet.y+.3f);
            if (!ground || ground->normal.y < .70710678f) continue;
            target.y = ground->height;
        }
        if (world.lineOfSight(feet+glm::vec3(0,.55f,0), target+glm::vec3(0,.55f,0))) return target;
    }
    return std::nullopt;
}

void DogLayer::selectWanderers(double now) {
    if (!nextWanderSelection_) nextWanderSelection_ = now+60;
    if (now < *nextWanderSelection_) return;
    // A delayed update samples once, without replaying missed minute batches.
    *nextWanderSelection_ += (std::floor((now-*nextWanderSelection_)/60)+1)*60;
    std::vector<std::size_t> population;
    for (std::size_t id = 0; id < instances.size(); ++id) {
        const auto& dog = instances[id];
        if (!dog.dead && !dog.removed && dog.asset < assets.size()) population.push_back(id);
    }
    const double expected = double(population.size())*.005;
    auto count = static_cast<std::size_t>(expected);
    if (std::bernoulli_distribution(expected-double(count))(wanderRandom_)) ++count;
    std::shuffle(population.begin(), population.end(), wanderRandom_);
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = population[i];
        auto& behavior = behaviors[id];
        if (behavior.state != DogState::idle || instances[id].attacking) continue;
        behavior = {};
        behavior.active = true;
        behavior.state = DogState::wandering;
        behavior.wander.remainingLegs = std::uniform_int_distribution<int>(3,7)(wanderRandom_);
        const float angle = std::uniform_real_distribution<float>(0,2*pi)(wanderRandom_);
        behavior.wander.direction = {std::sin(angle), std::cos(angle)};
        behavior.wander.previous = glm::vec3(instances[id].root[3]);
        behavior.wander.waypoints.push_back(instances[id].home.value_or(behavior.wander.previous));
        if (horizontalDistance(behavior.wander.waypoints.front(),behavior.wander.previous) > .01f)
            behavior.wander.waypoints.push_back(behavior.wander.previous);
        behavior.wander.nextPartCheck = now+5;
    }
}

bool DogLayer::prepareWanderStep(std::size_t id, const CollisionWorld& world, double now) {
    auto& behavior = behaviors[id];
    auto& wander = behavior.wander;
    const auto feet = glm::vec3(instances[id].root[3]);
    if (wander.waypoints.empty()) {
        wander.waypoints.push_back(instances[id].home.value_or(wander.previous));
        if (horizontalDistance(wander.waypoints.front(),wander.previous) > .01f) wander.waypoints.push_back(wander.previous);
    }
    if (wander.hasTarget) {
        const bool arrived = horizontalDistance(feet,behavior.target) <= arrivalDistance;
        if (!arrived && wander.stalledSeconds < 3) return true;
        if (wander.returning && !arrived) { finish(behavior,now); return false; }
        wander.hasTarget = false;
        if (arrived && !wander.returning) {
            --wander.remainingLegs;
            wander.waypoints.push_back(feet);
        }
        else wander.returning = true; // A clear sight ray need not be walkable.
    }
    if (wander.remainingLegs <= 0) wander.returning = true;
    if (wander.returning && huntingNavigation_) return beginHuntReturn(id,world,now);
    if (!wander.returning) {
        const float distance = std::uniform_real_distribution<float>(100,300)(wanderRandom_);
        if (const auto target = dogWanderTarget(feet,wander.direction,distance,world)) {
            wander.previous = feet;
            behavior.target = *target;
            wander.hasTarget = true;
            wander.stalledSeconds = 0;
            return true;
        }
        wander.returning = true;
    }
    if (wander.returning && huntingNavigation_) return beginHuntReturn(id,world,now);
    while (!wander.waypoints.empty() && horizontalDistance(feet,wander.waypoints.back()) <= arrivalDistance)
        wander.waypoints.pop_back();
    if (wander.waypoints.empty()) {
        finish(behavior,now);
        return false;
    }
    behavior.target = wander.waypoints.back();
    wander.hasTarget = true;
    wander.stalledSeconds = 0;
    return true;
}
} // namespace viewer
