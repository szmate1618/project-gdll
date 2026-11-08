#include "sword_combat.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "dog_layer.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
void SwordCombat::update(const SwordMelee& sword, const Camera& camera, double seconds,
        double animationSeconds, const CollisionWorld& world, ZombieLayer* zombies, DogLayer* dogs) {
    if (!std::isfinite(sword.startedAt())) { startedAt_ = sword.startedAt(); return; }
    if (startedAt_ != sword.startedAt()) {
        startedAt_ = sword.startedAt(); sampled_ = swordWindupSeconds;
        hitZombies_.clear(); hitDogs_.clear();
    }
    const float end = std::clamp(static_cast<float>(seconds - startedAt_),
        swordWindupSeconds, swordWindupSeconds + swordCutSeconds);
    if (end <= sampled_) return;
    std::vector<std::size_t> candidates;
    if (zombies) zombies->queryLivingNear(camera.position(), 2.5f, candidates);
    const auto eyeToWorld = glm::inverse(camera.view());
    const auto segment = [&](glm::vec3 a, glm::vec3 b) {
        const float length = glm::distance(a, b);
        if (length < 1e-6f) return;
        const auto direction = (b - a) / length;
        for (const auto id : candidates) {
            if (hitZombies_.count(id)) continue;
            const auto hit = zombies->traceShot(id, zombies->instances[id], a, direction, animationSeconds);
            if (!hit || hit->distance > length) continue;
            const auto point = a + direction * hit->distance;
            if (!world.lineOfSight(a, point) || !world.lineOfSight(camera.position(), point)) continue;
            if (zombies->applyReportedShot(hit->claim, a, direction, animationSeconds, world, ShotCaliber::high))
                hitZombies_.insert(id);
        }
        if (dogs) {
            const auto hit = dogs->traceShot(a, direction, world, animationSeconds);
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
