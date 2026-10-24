#pragma once
#include <cstddef>
#include <memory>
#include <glm/glm.hpp>

namespace viewer {
class PhysicsWorld;
class PhysicsScene;
class VehicleLayer;
class CollisionWorld;
class DoorLayer;
struct ZombieLayer;

// Local dynamic upright capsules, sharing the car's solver. ZombieLayer owns
// this component; it stores stable IDs, never pointers to movable instances.
class ZombieVehiclePhysics {
public:
    explicit ZombieVehiclePhysics(std::shared_ptr<PhysicsWorld> world);
    ~ZombieVehiclePhysics();
    void prepare(ZombieLayer& zombies, VehicleLayer& cars, PhysicsScene& scene,
                 const CollisionWorld& collision, float seconds, DoorLayer* doors);
    void finish(ZombieLayer& zombies, VehicleLayer& cars, float seconds, double animationSeconds);
    glm::vec3 release(ZombieLayer& zombies, std::size_t id); // Returns actual velocity for ragdoll handoff.
    void clear(ZombieLayer& zombies);
    std::size_t count() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
