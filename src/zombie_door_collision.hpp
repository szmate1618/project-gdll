#pragma once

#include <cstddef>
#include <memory>

namespace viewer {
class PhysicsWorld;
class DoorLayer;
struct ZombieLayer;

// Door-only kinematic capsules for living zombies near activated door sweeps.
// Feet remain authoritative in ZombieLayer; doors react through shared Box3D
// contacts. Prepare after door activation and before each shared physics step.
class ZombieDoorCollision {
public:
    explicit ZombieDoorCollision(std::shared_ptr<PhysicsWorld> world);
    ~ZombieDoorCollision();
    ZombieDoorCollision(const ZombieDoorCollision&) = delete;
    ZombieDoorCollision& operator=(const ZombieDoorCollision&) = delete;
    void prepare(const DoorLayer& doors, ZombieLayer& zombies);
    std::size_t count() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
