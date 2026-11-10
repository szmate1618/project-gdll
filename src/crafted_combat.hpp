#pragma once

#include "sword_melee.hpp"

namespace viewer {
class Item;
class CollisionWorld;
struct ZombieLayer;
class DogLayer;

// Uses the saved primitive volumes and palm frame, sharing the axe's input,
// windup, directional motion and first-contact recovery policy.
class CraftedCombat {
public:
    void update(SwordMelee& swing,const Item& item,const Camera& camera,double seconds,
                double animationSeconds,const CollisionWorld& world,ZombieLayer* zombies,DogLayer* dogs);
private:
    double startedAt_=-std::numeric_limits<double>::infinity();
    float sampled_=swordWindupSeconds;
    bool stopped_=false;
};
}
