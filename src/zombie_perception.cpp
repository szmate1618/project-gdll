#include "zombie_perception.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace viewer {
namespace {
double nextSlot(double now, double period, std::size_t zombie) {
    // Stable across dense-pool swap removals. Hash IDs to spread scheduled work.
    auto seed = static_cast<std::uint32_t>(zombie);
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    const double phase = static_cast<double>(seed) / 4294967296.0;
    return (std::floor(now / period - phase + 1e-9) + 1 + phase) * period;
}
}

double zombiePerceptionPeriod(double squaredDistance) {
    return squaredDistance <= 100.0 ? 0.1 : squaredDistance <= 900.0 ? 0.2 : 1.0;
}

bool ZombiePerceptionClock::due(double now, double squaredDistance, std::size_t zombie) {
    return dueEvery(now, zombiePerceptionPeriod(squaredDistance), zombie);
}

bool ZombiePerceptionClock::dueEvery(double now, double nextPeriod, std::size_t zombie) {
    if (!initialized) {
        initialized = true;
        period = nextPeriod;
        nextSeconds = nextSlot(now, period, zombie);
        return true; // Activation receives one immediate observation.
    }
    if (nextPeriod != period) {
        // Entering a faster band cannot retain a distant one-second deadline.
        // A slower band takes effect after at most one already scheduled check.
        if (nextPeriod < period) nextSeconds = std::min(nextSeconds, nextSlot(now, nextPeriod, zombie));
        period = nextPeriod;
    }
    if (now + 1e-9 < nextSeconds) return false;
    nextSeconds = nextSlot(now, period, zombie);
    return true; // At most one observation; never replay missed checks after a stall.
}

ZombieSight sampleZombieSight(const ZombieInstance& instance, const ZombiePlayer& player,
                             const CollisionWorld& world, std::size_t& rayCount, bool anyClearEnough) {
    const auto feet = glm::vec3(instance.restRoot[3]);
    if (!zombiePlayerInFov(feet, glm::vec3(instance.restRoot[2]), player.feet)) return ZombieSight::none;
    const auto eyes = feet + glm::vec3(0, 1.65f, 0);
    int clear = 0, blocked = 0;
    for (const float height : {player.eyeHeight, player.height * 0.5f, 0.0f}) {
        ++rayCount;
        if (world.lineOfSight(eyes, player.feet + glm::vec3(0, height, 0))) ++clear;
        else ++blocked;
        if ((anyClearEnough && clear) || (clear && blocked)) return ZombieSight::partial;
    }
    return clear == 3 ? ZombieSight::full : ZombieSight::none;
}
} // namespace viewer
