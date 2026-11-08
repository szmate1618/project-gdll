#include "sword_combat.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "dog_layer.hpp"
#include "dog_hit_query.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
void SwordCombat::update(const SwordMelee& sword, const Camera& camera, double seconds,
        double animationSeconds, const CollisionWorld& world, ZombieLayer* zombies, DogLayer* dogs) {
    if (!std::isfinite(sword.startedAt())) { startedAt_ = sword.startedAt(); return; }
    if (startedAt_ != sword.startedAt()) {
        startedAt_ = sword.startedAt(); sampled_ = swordWindupSeconds;
        hitZombies_.clear(); hitDogs_.clear();
        severedZombies_.clear(); createdCorpses_.clear();
    }
    const float end = std::clamp(static_cast<float>(seconds - startedAt_),
        swordWindupSeconds, swordWindupSeconds + swordCutSeconds);
    if (end <= sampled_) return;
    std::vector<std::size_t> candidates;
    if (zombies) zombies->queryLivingNear(camera.position(), 2.5f, candidates);
    if (zombies) for (const auto& [id, handle] : createdCorpses_)
        if (id < zombies->instances.size() && zombies->instances[id].ragdoll && zombies->instances[id].ragdollHandle == handle)
            candidates.push_back(id);
    DogMeleeQuery dogContacts(dogs, camera.position(), 2.5f, animationSeconds);
    const auto eyeToWorld = glm::inverse(camera.view());
    const auto segment = [&](glm::vec3 a, glm::vec3 b) {
        const float length = glm::distance(a, b);
        if (length < 1e-6f) return;
        const auto direction = (b - a) / length;
        const auto clear = [&](glm::vec3 point) {
            return world.lineOfSight(a, point) && world.lineOfSight(camera.position(), point);
        };
        for (const auto id : candidates) {
            const auto& instance = zombies->instances[id];
            if (instance.removed || severedZombies_.count(id)) continue;
            const auto corpse = createdCorpses_.find(id);
            if (instance.ragdoll && (corpse == createdCorpses_.end() || corpse->second != instance.ragdollHandle)) continue;
            const auto joint = zombies->traceMeleeJoint(id, a, b, animationSeconds);
            const auto body = hitZombies_.count(id) ? std::nullopt : zombies->traceMeleeBody(id, a, b, animationSeconds);
            const auto sever = [&] {
                if (!joint) return false;
                const auto point = a + direction * joint->distance;
                if (!clear(point) || !zombies->severJoint(id, joint->joint, point, direction, animationSeconds, world)) return false;
                severedZombies_.insert(id);
                return true;
            };
            const bool jointFirst = joint && (!body || joint->distance <= body->distance);
            if (jointFirst && sever()) continue;
            if (body && clear(a + direction * body->distance) &&
                zombies->applyReportedShot(body->claim, a, direction, animationSeconds, world, ShotCaliber::high)) {
                hitZombies_.insert(id);
                if (instance.ragdoll) createdCorpses_.emplace(id, instance.ragdollHandle);
            }
            if (!jointFirst) sever();
        }
        if (dogs) {
            const auto hit = dogContacts.trace(a, b, world);
            if (hit && hit->distance <= length && !hitDogs_.count(hit->dog) &&
                world.lineOfSight(camera.position(), a + direction * hit->distance)) {
                dogs->damage(hit->dog, .2f); hitDogs_.insert(hit->dog);
            }
        }
    };
    const auto frame = [&](float elapsed) {
        auto pose = sword.pose(startedAt_); pose.elapsed = elapsed;
        return eyeToWorld * swordTransform(pose);
    };
    auto previous = frame(sampled_);
    // At most 47 samples per cut, independent of frame rate or input cadence.
    while (sampled_ < end) {
        const float next = std::min(end, sampled_ + 1.f / 180);
        const auto current = frame(next);
        segment(glm::vec3(current * glm::vec4(0, swordBladeBase, 0, 1)),
                glm::vec3(current * glm::vec4(0, swordBladeTip, 0, 1)));
        // Connect previous and current blade positions to catch thin limbs.
        for (int i = 0; i <= 10; ++i) {
            const glm::vec4 point(0, glm::mix(swordBladeBase, swordBladeTip, i / 10.f), 0, 1);
            segment(glm::vec3(previous * point), glm::vec3(current * point));
        }
        previous = current; sampled_ = next;
    }
}
}
