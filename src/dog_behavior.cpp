#include "dog_layer.hpp"
#include "character_step.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr float pi = 3.14159265359f;
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
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
bool clear(glm::vec3 feet, const CollisionWorld& world) {
    for (const auto& c : world.contacts(feet, dogRadius, dogHeight, .002f, .05f)) {
        if (c.depth <= .0001f || (c.terrainSupport && c.normal.y >= .70710678f) ||
            (c.normal.y > .7f && c.point.y <= feet.y+.0021f)) continue;
        return false;
    }
    return true;
}
}
glm::vec3 DogLayer::move(std::size_t id, glm::vec3 desired, const CollisionWorld& world) {
    auto feet = glm::vec3(instances[id].root[3]);
    const auto offset = desired-feet;
    const float distance = glm::length(glm::vec2(offset.x,offset.z));
    if (distance < 1e-6f) return feet;
    const int segments = std::min(2048, int(std::ceil(distance/(dogRadius/3))));
    const auto delta = offset / float(segments);
    WalkingGroundCache groundCache;
    for (int segment = 0; segment < segments; ++segment) {
        auto next = feet+delta;
        if (!world.empty()) {
            const auto ground = world.walkingGroundAt(next.x, next.z, std::numeric_limits<float>::lowest(), feet.y+.3f, &groundCache);
            if (!ground || ground->normal.y < .70710678f) break;
            next.y = ground->height;
            if (!clear(next, world)) {
                const CharacterStepConfig config{dogRadius,dogHeight,.3f,.70710678f,.002f,.05f};
                const auto step = tryCharacterStep(feet,next,world,config);
                if (!step) break;
                next = step->feet;
            }
        }
        spatial_.query(next, dogRadius*2, candidates_);
        bool blocked = false;
        for (const auto other : candidates_) {
            if (other == id) continue;
            const auto p = glm::vec3(instances[other].root[3]);
            if (std::abs(p.y-next.y) >= dogHeight) continue;
            if (glm::length(glm::vec2(p.x-next.x,p.z-next.z)) < dogRadius*2-.0001f) { blocked = true; break; }
        }
        if (blocked) break;
        feet = next;
    }
    return feet;
}
void DogLayer::makeNoise(glm::vec3 position, float decibels) {
    if (!finite(position) || !std::isfinite(decibels)) return;
    for (std::size_t id = 0; id < behaviors.size(); ++id) {
        auto& behavior = behaviors[id];
        const auto& dog = instances[id];
        if (!behavior.active || behavior.state != DogState::idle || dog.dead || dog.removed) continue;
        const auto feet = glm::vec3(dog.root[3]);
        if (noiseLevelAt({position,decibels},feet) < zombieHearingThresholdDecibels) continue;
        const auto offset = glm::vec2(position.x-feet.x,position.z-feet.z);
        if (glm::length(offset) < 1e-4f) continue;
        behavior.lookDirection = glm::normalize(offset);
        behavior.state = DogState::looking;
    }
}
void DogLayer::updateBehavior(float dt, const std::vector<ZombiePlayer>& players, const CollisionWorld& world, double now) {
    hits_.clear();
    if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(now)) return;
    for (const auto& p : players) if (!finite(p.feet) || !finite(p.velocity)) return;
    if (behaviors.size() != instances.size()) rebuildSpatialIndex();
    for (std::size_t id = 0; id < instances.size(); ++id) {
        auto& dog = instances[id]; auto& behavior = behaviors[id];
        if (dog.dead || dog.removed || dog.asset >= assets.size()) continue;
        dog.walking = dog.running = false;
        auto feet = glm::vec3(dog.root[3]);
        const ZombiePlayer* nearest = nullptr;
        float closest = std::numeric_limits<float>::max();
        for (const auto& player : players) {
            const auto delta = player.feet-feet;
            const float d = glm::dot(delta,delta);
            if (d < closest) { closest = d; nearest = &player; }
        }
        if (!nearest) { dog.attacking = false; behavior = {}; continue; }
        if (!behavior.active) {
            if (closest > 100*100) continue;
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
                else if (behavior.state == DogState::idle || behavior.state == DogState::looking) behavior.state = DogState::investigating;
            }
        }
        const ZombiePlayer* target = nullptr;
        for (const auto& player : players) if (player.id == behavior.targetPlayer) target = &player;
        const auto eligible = [&] {
            return target && glm::length(target->feet-feet) <= 1.5f && sight(dog,*target,world) > 0;
        };
        if (dog.attacking) {
            dog.attackSeconds = now-behavior.attackStarted;
            const auto duration = assets[dog.asset].attackLeftClip ? assets[dog.asset].attackLeftClip->duration : .8f;
            if (!behavior.attackHit && dog.attackSeconds >= duration*.45f) {
                behavior.attackHit = true;
                if (eligible()) hits_.push_back({id,target->id,10.f});
            }
            if (dog.attackSeconds < duration) continue;
            dog.attacking = false; behavior.readyAt = now+.5;
            behavior.state = behavior.sight == 2 ? DogState::chasing : DogState::investigating;
        }
        if (eligible() && glm::length(target->feet-feet) <= 1.f) {
            if (now >= behavior.readyAt) {
                dog.attacking = true; dog.attackSeconds = 0;
                behavior.attackStarted = now; behavior.attackHit = false; behavior.state = DogState::attacking;
            }
            continue;
        }
        if (behavior.state == DogState::idle || behavior.state == DogState::looking) {
            if (behavior.state == DogState::idle && now-behavior.idleSince >= 10) behavior = {};
            continue;
        }
        auto destination = behavior.target;
        const auto horizontal = glm::vec2(destination.x-feet.x,destination.z-feet.z);
        const float distance = glm::length(horizontal);
        const float speed = behavior.state == DogState::chasing ? 3.f : 1.2f;
        float travel = std::min(speed*dt,distance);
        if (behavior.sight && std::abs(destination.y-feet.y) < 1.f) travel = std::min(travel,std::max(0.f,distance-1.f));
        if (distance > 1e-4f && travel > 1e-6f) {
            const auto direction = horizontal/distance;
            const auto next = move(id,feet+glm::vec3(direction.x*travel,0,direction.y*travel),world);
            const auto moved = glm::vec2(next.x-feet.x,next.z-feet.z);
            if (glm::length(moved) >= .1f*dt) {
                dog.root[3] = glm::vec4(next,1); face(dog,moved,dt,pi);
                dog.walking = true; dog.running = speed > 1.2f;
                dog.walkSeconds += glm::length(moved)/speed;
                spatial_.update(id,next);
            }
        } else if (!behavior.sight && distance <= 1e-4f) {
            behavior.state = DogState::idle; behavior.idleSince = now;
        }
    }
}
} // namespace viewer
