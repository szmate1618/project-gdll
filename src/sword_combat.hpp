#pragma once
#include "sword_melee.hpp"
#include <set>

namespace viewer {
class CollisionWorld;
struct ZombieLayer;
class DogLayer;
// Server/standalone ownership. Tests finite swept blade segments against nearby
// posed actors; each actor takes damage at most once per cut.
class SwordCombat {
public:
    void update(const SwordMelee& sword, const Camera& camera, double seconds,
        double animationSeconds, const CollisionWorld& world, ZombieLayer* zombies, DogLayer* dogs);
private:
    double startedAt_ = -std::numeric_limits<double>::infinity();
    float sampled_ = swordWindupSeconds;
    std::set<std::size_t> hitZombies_, hitDogs_;
};
}
