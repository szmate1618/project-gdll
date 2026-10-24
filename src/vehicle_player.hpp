#pragma once
#include "vehicle_layer.hpp"
#include "camera_rig.hpp"
#include <functional>

namespace viewer {
class PhysicsScene;
// Placement and local seat/camera state are separate from rigid bodies and rendering.
bool placeVehicleNear(VehicleLayer& cars, const CollisionWorld& world,
                      glm::vec3 preferredEye, glm::vec3 minimum, glm::vec3 maximum, std::size_t type = 0);
void prepareVehicleScene(const VehicleLayer& cars, PhysicsScene& scene);
glm::vec3 vehicleSeat(const BoxPose& chassis); // Default prototype driver seat.
glm::vec3 vehicleSeat(const VehicleLayer& cars, std::size_t car, std::size_t seat);
struct VehicleSeatRef { std::size_t car = 0, seat = 0; };
std::optional<VehicleSeatRef> vehicleEntry(const VehicleLayer& cars, glm::vec3 eye, const CollisionWorld& world,
    const std::function<bool(std::size_t, std::size_t)>& available = {});
std::string vehicleEntryLabel(const VehicleLayer& cars, const VehicleSeatRef& entry);
std::vector<glm::vec3> vehicleExits(const VehicleLayer& cars, std::size_t car,
                                  const CollisionWorld& world, const FPSConfig& config, std::size_t seat = 0);
class VehiclePlayer {
public:
    std::optional<std::size_t> car() const { return car_; }
    bool occupied() const { return car_.has_value(); }
    bool driving() const { return occupied() && seat_ == 0; }
    std::size_t seat() const { return seat_; }
    bool thirdPerson() const { return thirdPerson_; }
    bool toggle(VehicleLayer& cars, CameraRig& rig, const CollisionWorld& world);
    bool toggleThirdPerson(const VehicleLayer& cars, Camera& camera, const CollisionWorld& world);
    void updateCamera(const VehicleLayer& cars, Camera& camera, const CollisionWorld& world);
    glm::vec3 eyePosition(const VehicleLayer& cars) const; // Occupied seat, independent of camera mode.
    // Adopt server-confirmed occupancy; camera mode remains local to this client.
    void followCar(std::optional<std::size_t> car, const VehicleLayer& cars, Camera& camera, std::size_t seat = 0);
private:
    std::optional<std::size_t> car_;
    std::size_t seat_ = 0;
    float lastYaw_ = 0;
    bool thirdPerson_ = false;
    float firstPersonPitch_ = 0;
};
} // namespace viewer
