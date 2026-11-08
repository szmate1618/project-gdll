#pragma once
#include "sword_melee.hpp"
#include <set>
#include <map>

namespace viewer {
class CollisionWorld;
struct ZombieLayer;
class DogLayer;
// Server/standalone ownership. Tests finite swept blade segments against nearby
// posed actors; each actor takes ordinary damage and severs at most once per cut.
class SwordCombat {
public:
    void update(const SwordMelee& sword, const Camera& camera, double seconds,
        double animationSeconds, const CollisionWorld& world, ZombieLayer* zombies, DogLayer* dogs);
private:
    double startedAt_ = -std::numeric_limits<double>::infinity();
    float sampled_ = swordWindupSeconds;
    std::set<std::size_t> hitZombies_, hitDogs_;
    std::set<std::size_t> severedZombies_;
    std::map<std::size_t, std::size_t> createdCorpses_; // Actor ID -> owned ragdoll handle, only for this cut.
};
}
