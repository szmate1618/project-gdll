#pragma once
#include "vehicle_layer.hpp"
#include "camera_rig.hpp"

namespace viewer {
class PhysicsScene;
class ZombieLayer;
// Placement and driver state are separate from rigid bodies and rendering.
bool placeVehicleNear(VehicleLayer& cars, const CollisionWorld& world,
                      glm::vec3 preferredEye, glm::vec3 minimum, glm::vec3 maximum);
void prepareVehicleScene(const VehicleLayer& cars, PhysicsScene& scene);
class VehiclePlayer {
public:
    std::optional<std::size_t> car() const { return car_; }
    bool driving() const { return car_.has_value(); }
    bool toggle(VehicleLayer& cars, CameraRig& rig, const CollisionWorld& world);
    void updateCamera(const VehicleLayer& cars, Camera& camera);
private:
    std::optional<std::size_t> car_;
    float lastYaw_ = 0;
};
// Sweeps the chassis between fixed-step poses, using the living zombie index.
void collideVehicleZombies(VehicleLayer& cars, std::size_t car, const BoxPose& previous,
                          ZombieLayer& zombies, const CollisionWorld& world,
                          float seconds, double animationSeconds);
} // namespace viewer
