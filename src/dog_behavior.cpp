#include "dog_layer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr float pi = 3.14159265359f;
constexpr float approachDistance = 1.f;
constexpr float approachTolerance = .0001f;
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool roaming(DogState state) { return state == DogState::wandering || state == DogState::fetching || state == DogState::hunting; }
void face(DogInstance& dog, glm::vec2 direction, float dt, float turnRate) {
    if (glm::length(direction) < 1e-6f) return;
    float yaw = std::atan2(dog.root[2].x, dog.root[2].z);
    yaw += std::clamp(std::remainder(std::atan2(direction.x, direction.y)-yaw, 2*pi), -turnRate*dt, turnRate*dt);
    dog.root = glm::translate(glm::mat4(1), glm::vec3(dog.root[3])) * glm::rotate(glm::mat4(1), yaw, {0,1,0});
    dog.transform = dog.root;
}
int sight(const DogInstance& dog, const ZombiePlayer& player, const CollisionWorld& world) {
    const auto feet = glm::vec3(dog.root[3]);
    if (!zombiePlayerInFov(feet, glm::vec3(dog.root[2]), player.feet)) return 0;
    int clear = 0;
    for (const float height : {player.eyeHeight, player.height*.5f, 0.f})
        if (world.lineOfSight(feet+glm::vec3(0,.55f,0), player.feet+glm::vec3(0,height,0))) ++clear;
    return clear == 3 ? 2 : clear ? 1 : 0;
}
}
void DogLayer::makeNoise(const NoiseEvent& noise) {
    if (noise.audience == NoiseAudience::everyone) makeNoise(noise.position, noise.decibels);
}
void DogLayer::makeNoise(glm::vec3 position, float decibels) {
    if (!finite(position) || !std::isfinite(decibels)) return;
    for (std::size_t id = 0; id < behaviors.size(); ++id) {
        auto& behavior = behaviors[id];
        const auto& dog = instances[id];
        if (!behavior.active || (behavior.state != DogState::idle && !roaming(behavior.state)) ||
            dog.dead || dog.removed) continue;
        const auto feet = glm::vec3(dog.root[3]);
        if (noiseLevelAt({position,decibels},feet) < zombieHearingThresholdDecibels) continue;
        const auto offset = glm::vec2(position.x-feet.x,position.z-feet.z);
        if (glm::length(offset) < 1e-4f) continue;
        behavior.lookDirection = glm::normalize(offset);
        cancelHunt(id);
        behavior.wander = {};
        behavior.fetch.reset();
        behavior.state = DogState::looking;
    }
}
void DogLayer::updateBehavior(float dt, const std::vector<ZombiePlayer>& players, const CollisionWorld& world, double now,
                              ZombieDetachedParts* parts) {
    hits_.clear();
    if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(now)) return;
    for (const auto& p : players) if (!finite(p.feet) || !finite(p.velocity)) return;
    if (behaviors.size() != instances.size()) rebuildSpatialIndex();
    doorways_.beginStep(dt);
    huntAttemptsRemaining_ = 4; // At most twelve scent-destination path attempts per step.
    selectWanderers(now);
    for (std::size_t id = 0; id < instances.size(); ++id) {
        auto& dog = instances[id]; auto& behavior = behaviors[id];
        if (dog.dead || dog.removed || dog.asset >= assets.size()) {
            cancelHunt(id); behavior.fetch.reset(); spatial_.erase(id); doorways_.erase(id); continue;
        }
        dog.walking = dog.running = false;
        auto feet = glm::vec3(dog.root[3]);
        const ZombiePlayer* nearest = nullptr;
        float closest = std::numeric_limits<float>::max();
        for (const auto& player : players) {
            const auto delta = player.feet-feet;
            const float d = glm::dot(delta,delta);
            if (d < closest) { closest = d; nearest = &player; }
        }
        if (!nearest) {
            dog.attacking = false;
            if (!roaming(behavior.state) && behavior.state != DogState::idle && behavior.state != DogState::looking) behavior = {};
            behavior.suspended = true;
            behavior.sight = 0;
        } else if (!behavior.active) {
            if (closest > 100*100 && !tryStartHunt(id,world,now)) continue;
            behavior.active = true; behavior.idleSince = now;
        }
        if (closest > 150*150) { behavior.suspended = true; behavior.sight = 0; }
        else if (behavior.suspended && closest <= 100*100) { behavior.suspended = false; behavior.perception = {}; }
        if (behavior.state == DogState::looking) {
            face(dog,behavior.lookDirection,dt,pi/2);
            if (glm::dot(glm::vec2(dog.root[2].x,dog.root[2].z),behavior.lookDirection) > .9999f)
                behavior.state = DogState::idle;
        }
        if (!behavior.suspended && behavior.perception.due(now,closest,id)) {
            const ZombiePlayer* selected = nullptr; int visibility = 0; float best = std::numeric_limits<float>::max();
            for (const auto& player : players) {
                const auto offset = player.feet-feet; const float distance = glm::dot(offset,offset);
                if (distance > 150*150) continue;
                const int visible = sight(dog,player,world);
                if (!visible) continue;
                if (player.id == behavior.targetPlayer) { selected = &player; visibility = visible; break; }
                if (distance < best) { selected = &player; visibility = visible; best = distance; }
            }
            behavior.sight = visibility;
            if (selected) {
                behavior.target = selected->feet; behavior.targetPlayer = selected->id;
                if (visibility == 2 && behavior.state != DogState::attacking) behavior.state = DogState::chasing;
                else if (behavior.state == DogState::idle || behavior.state == DogState::looking ||
                         roaming(behavior.state)) behavior.state = DogState::investigating;
                behavior.wander = {};
                behavior.fetch.reset();
                cancelHunt(id);
            }
        }
        const ZombiePlayer* target = nullptr;
        for (const auto& player : players) if (player.id == behavior.targetPlayer) target = &player;
        const auto eligible = [&] {
            return !roaming(behavior.state) && target &&
                glm::length(target->feet-feet) <= 1.5f && sight(dog,*target,world) > 0;
        };
        if (dog.attacking) {
            dog.attackSeconds = now-behavior.attackStarted;
            const auto duration = assets[dog.asset].attackLeftClip ? assets[dog.asset].attackLeftClip->duration : .8f;
            if (!behavior.attackHit && dog.attackSeconds >= duration*.45f) {
                behavior.attackHit = true;
                if (eligible()) hits_.push_back({id,target->id,.1f});
            }
            if (dog.attackSeconds < duration) continue;
            dog.attacking = false; behavior.readyAt = now+.5;
            behavior.state = behavior.sight == 2 ? DogState::chasing : DogState::investigating;
        }
        if (eligible() && glm::length(target->feet-feet) <= approachDistance+approachTolerance) {
            if (now >= behavior.readyAt) {
                dog.attacking = true; dog.attackSeconds = 0;
                behavior.attackStarted = now; behavior.attackHit = false; behavior.state = DogState::attacking;
            }
            continue;
        }
        tryStartHunt(id,world,now);
        if (behavior.state == DogState::idle || behavior.state == DogState::looking) {
            if (behavior.state == DogState::idle && now-behavior.idleSince >= 10) behavior = {};
            continue;
        }
        if (behavior.state == DogState::wandering && parts) findFetchPart(id,*parts,world,now);
        if (behavior.state == DogState::wandering && !prepareWanderStep(id,world,now)) continue;
        if (behavior.state == DogState::fetching && !prepareFetchStep(id,parts,world,now)) continue;
        if (behavior.state == DogState::hunting && !prepareHuntStep(id,world,now)) continue;
        const bool hunting = behavior.state == DogState::hunting;
        const auto goal = behavior.target;
        const float goalDistance = glm::length(glm::vec2(goal.x-feet.x,goal.z-feet.z));
        const auto passage = hunting ? std::nullopt : doorways_.destination(id,feet,goal,world);
        const auto destination = passage.value_or(goal);
        const auto horizontal = glm::vec2(destination.x-feet.x,destination.z-feet.z);
        const float distance = glm::length(horizontal);
        const float speed = behavior.state == DogState::chasing ? 3.f : 1.2f;
        float travel = std::min(speed*dt,distance);
        glm::vec2 routeVelocity(0);
        if (hunting) {
            routeVelocity = huntingNavigation_->follow(id,feet,speed,dt,world);
            travel = glm::length(routeVelocity)*dt;
        }
        const float heightDifference = goal.y-feet.y;
        if (behavior.sight && std::abs(heightDifference) < approachDistance) {
            // Match the 3D attack range, including terrain and player foot lift.
            const float horizontalGap = std::sqrt(approachDistance*approachDistance-heightDifference*heightDifference);
            travel = std::min(travel,std::max(0.f,goalDistance-horizontalGap));
        }
        if (distance > 1e-4f && travel > 1e-6f) {
            const auto forward = hunting ? glm::normalize(routeVelocity) : horizontal/distance;
            auto direction = passage ? forward : steeringDirection(id,hunting ? forward*std::max(distance,.15f) : horizontal,world);
            const auto desired = [&](glm::vec2 heading) { return feet+glm::vec3(heading.x*travel,0,heading.y*travel); };
            if (hunting && !huntingNavigation_->segmentClear(feet,desired(direction))) direction = forward;
            auto next = move(id,desired(direction),world);
            if (hunting && !huntingNavigation_->movementClear(id,feet,next)) next = feet;
            const auto moved = glm::vec2(next.x-feet.x,next.z-feet.z);
            if (next != feet) {
                // Physical progress is independent of the animation speed gate.
                dog.root[3] = glm::vec4(next,1);
                dog.transform = dog.root;
                spatial_.update(id,next);
            }
            if (glm::length(moved) >= .1f*dt) {
                face(dog,moved,dt,pi);
                dog.walking = true; dog.running = speed > 1.2f;
                dog.walkSeconds += glm::length(moved)/(dog.running ? dogRunAnimationSpeed : dogWalkAnimationSpeed);
            }
        } else if (!roaming(behavior.state) && !behavior.sight && goalDistance <= 1e-4f) {
            behavior.state = DogState::idle; behavior.idleSince = now;
        }
        if (roaming(behavior.state)) {
            const auto next = glm::vec3(dog.root[3]);
            if (hunting) huntingNavigation_->recordMovement(id,feet,next,dt);
            const float remaining = glm::length(glm::vec2(goal.x-next.x,goal.z-next.z));
            auto& stalled = hunting ? behavior.hunt->stalledSeconds :
                behavior.state == DogState::wandering ? behavior.wander.stalledSeconds : behavior.fetch->stalledSeconds;
            const float progress = hunting ? glm::length(glm::vec2(next.x-feet.x,next.z-feet.z)) : goalDistance-remaining;
            if (!passage && progress < .1f*dt) stalled += dt;
            else stalled = 0;
            if (behavior.fetch && behavior.fetch->grip)
                behavior.fetch->grip->setTarget(dogMouthPosition(dog,assets[dog.asset]));
        }
    }
}
} // namespace viewer
