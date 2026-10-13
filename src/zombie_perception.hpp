#pragma once

#include <cstddef>

namespace viewer {
struct ZombieInstance;
struct ZombiePlayer;
class CollisionWorld;

enum class ZombieSight { none, partial, full };

// Polled synchronously from the simulation step. No timers, tasks, or threads.
struct ZombiePerceptionClock {
    bool initialized = false;
    double nextSeconds = 0, period = 0;
    bool due(double now, double squaredDistance, std::size_t zombie);
};

double zombiePerceptionPeriod(double squaredDistance);
// anyClearEnough is for every-step chasing only: partial then means at least
// one clear ray, and must not be reused as a complete cached classification.
ZombieSight sampleZombieSight(const ZombieInstance& instance, const ZombiePlayer& player,
                             const CollisionWorld& world, std::size_t& rayCount,
                             bool anyClearEnough = false);
} // namespace viewer
