#pragma once
#include "sword_melee.hpp"
#include <map>
#include <set>

namespace viewer {
class CollisionWorld;
struct ZombieLayer;
class DogLayer;
// Authoritative finite head/handle sweeps. Only the leading edge can sever;
// the earliest solid zombie contact stops both further damage and animation.
class AxeCombat {
public:
    void update(SwordMelee& axe, const Camera& camera, double seconds,
        double animationSeconds, const CollisionWorld& world, ZombieLayer* zombies, DogLayer* dogs);
private:
    double startedAt_ = -std::numeric_limits<double>::infinity();
    float sampled_ = swordWindupSeconds;
    bool stopped_ = false;
    std::set<std::size_t> severed_, hitDogs_;
    std::map<std::size_t, std::size_t> createdCorpses_;
};
}
