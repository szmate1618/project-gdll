#pragma once
#include "vehicle_layer.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>

namespace viewer {
// Internal representation owned by VehicleLayer. Parked cars have one static
// body with two boxes; detailed cars additionally own four wheels and joints.
// The world and config must outlive this object. All positions are Y-up meters.
struct VehiclePhysics {
    VehiclePhysics(PhysicsWorld& world, const VehicleConfig& config, glm::vec3 center, glm::quat orientation);
    ~VehiclePhysics();
    VehiclePhysics(const VehiclePhysics&) = delete;
    VehiclePhysics& operator=(const VehiclePhysics&) = delete;
    void activate(const b3HullData* tire);
    void park();
    VehiclePose pose(std::size_t index) const;
    void setPose(const VehiclePose& pose, glm::vec3 velocity);
    BoxPose collider(std::size_t index, std::size_t part) const;
    bool canPark(float seconds);

    PhysicsWorld& world;
    const VehicleConfig& config;
    std::size_t type = 0;
    std::uint64_t revision = 1;
    b3BodyId body{};
    std::array<b3BodyId, 4> wheels{};
    std::array<b3JointId, 4> joints{};
    std::array<b3ShapeId, 4> tireShapes{};
    std::array<b3ShapeId, 2> boxes{};
    std::array<WheelTraction, 4> traction{};
    std::array<glm::mat4, 4> storedWheels{};
    glm::vec3 replicatedVelocity{0};
    float throttle = 0, steering = 0, steeringAngle = 0, opposingImpulse = 0;
    float settlingSeconds = 0;
    bool detailed = false, driven = false, brake = true;
private:
    void replaceBoxes(bool detailed);
    void destroyWheels();
};
} // namespace viewer
