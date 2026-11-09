#pragma once

#include "ground_items.hpp"
#include <memory>

namespace viewer {
struct ZombieLayer;
class CollisionWorld;

// Authoritative fixed-step flight/embedding for ground-inventory stakes.
// Borrows the inventory, scene and optional population; all must outlive it.
// Replicas consume ordinary ground-item poses and never run this simulation.
class StakeProjectiles {
public:
    StakeProjectiles(GroundItems& items, std::shared_ptr<PhysicsWorld> world,
                     const CollisionWorld& collision, ZombieLayer* zombies = nullptr);
    ~StakeProjectiles();
    StakeProjectiles(const StakeProjectiles&) = delete;
    StakeProjectiles& operator=(const StakeProjectiles&) = delete;
    void setAnimationSeconds(double seconds);
    bool embedded(std::uint64_t id) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
