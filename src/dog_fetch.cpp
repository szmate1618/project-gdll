#include "dog_layer.hpp"
#include "zombie_detached_parts.hpp"
#include "physics_scene.hpp"
#include "door_layer.hpp"
#include <cmath>

namespace viewer {
namespace {
float horizontalDistance(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x-b.x,a.z-b.z)); }
void finishFetch(DogBehavior& behavior, double now) {
    behavior.fetch.reset(); // Release the mouth joint and the part's claim.
    behavior.state = DogState::idle;
    behavior.idleSince = now;
}
} // namespace
glm::vec3 dogMouthPosition(const DogInstance& dog, const AnimatedModel& asset) {
    for (std::size_t i = 0; i < asset.skeleton.size(); ++i) {
        if (asset.skeleton[i].name != "jaw") continue;
        const auto pose = sampleAnimatedPose(asset,dog.walkSeconds,dog.phase,dogAnimationClip(asset,dog));
        // Jaw-local mouth point; follows existing walking motion, with no new clip.
        return glm::vec3(dog.transform*pose[i]*glm::vec4(0,.10f,.015f,1));
    }
    return glm::vec3(dog.root*glm::vec4(0,.55f,.55f,1));
}

void DogLayer::findFetchPart(std::size_t id, ZombieDetachedParts& parts, const CollisionWorld& world, double now) {
    auto& behavior = behaviors[id];
    if (now < behavior.wander.nextPartCheck) return;
    behavior.wander.nextPartCheck = now+5;
    const auto feet = glm::vec3(instances[id].root[3]);
    const auto eyes = feet+glm::vec3(0,.55f,0);
    std::vector<DetachedZombiePart> candidates;
    parts.queryNear(feet,100,10,candidates);
    for (const auto& part : candidates) {
        if (!world.lineOfSight(eyes,part.position)) continue;
        DogFetching fetch;
        fetch.part = part.body;
        fetch.homeRoute = behavior.wander.waypoints;
        if (fetch.homeRoute.empty()) fetch.homeRoute.push_back(instances[id].home.value_or(behavior.wander.previous));
        fetch.homeRoute.push_back(feet);
        behavior.fetch = std::move(fetch);
        behavior.wander = {};
        behavior.state = DogState::fetching;
        behavior.sight = 0;
        behavior.target = part.position;
        return;
    }
}

bool DogLayer::prepareFetchStep(std::size_t id, ZombieDetachedParts* parts, const CollisionWorld& world, double now) {
    auto& behavior = behaviors[id];
    if (!behavior.fetch) { finishFetch(behavior,now); return false; }
    auto& fetch = *behavior.fetch;
    const auto feet = glm::vec3(instances[id].root[3]);
    const auto mouth = dogMouthPosition(instances[id],assets[instances[id].asset]);
    if (fetch.grip && !fetch.grip->valid()) fetch.grip.reset();
    if (!fetch.returning) {
        const auto target = parts ? parts->availablePosition(fetch.part) : std::nullopt;
        if (!target || fetch.stalledSeconds >= 3) fetch.returning = true;
        else {
            behavior.target = *target;
            if (horizontalDistance(feet,*target) <= .8f && glm::distance(mouth,*target) <= 1.2f &&
                world.lineOfSight(mouth,*target)) {
                fetch.grip = parts->carry(fetch.part,mouth);
                fetch.returning = true;
            } else return true;
        }
        fetch.stalledSeconds = 0;
    }
    if (fetch.stalledSeconds >= 3) { finishFetch(behavior,now); return false; }
    while (!fetch.homeRoute.empty() && horizontalDistance(feet,fetch.homeRoute.back()) <= .01f)
        fetch.homeRoute.pop_back();
    if (fetch.homeRoute.empty()) { finishFetch(behavior,now); return false; }
    behavior.target = fetch.homeRoute.back();
    return true;
}

void DogLayer::prepareFetchPhysics(PhysicsScene& scene, DoorLayer* doors) const {
    for (std::size_t id = 0; id < behaviors.size(); ++id) {
        const auto& behavior = behaviors[id];
        if (!behavior.fetch || !behavior.fetch->grip || !behavior.fetch->grip->valid()) continue;
        const auto& dog = instances[id];
        const auto mouth = dogMouthPosition(dog,assets[dog.asset]);
        scene.ensureNear(mouth-glm::vec3(2),mouth+glm::vec3(2));
        if (doors) doors->activateNear(mouth,3);
    }
}
} // namespace viewer
