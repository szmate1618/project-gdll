#pragma once

#include "box_layer.hpp"
#include "vehicle_traction.hpp"
#include "vehicle_config.hpp"
#include <array>

namespace viewer {
// Runtime meters, Y up; the car's local forward is -Z, wheel axle is X.
// Parked cars use two static boxes. Driven cars own a chassis and four wheels.
// The shared physics world must outlive this layer.
struct VehiclePose {
    BoxPose chassis;
    std::array<glm::mat4, 4> wheels;
};
class VehicleLayer {
public:
    explicit VehicleLayer(std::shared_ptr<PhysicsWorld> world, std::vector<VehicleConfig> types = {VehicleConfig{}});
    ~VehicleLayer();
    VehicleLayer(const VehicleLayer&) = delete;
    VehicleLayer& operator=(const VehicleLayer&) = delete;
    std::size_t add(glm::vec3 center, float yawRadians = 0, std::size_t type = 0);
    std::size_t add(glm::vec3 center, glm::quat rotation, std::size_t type = 0);
    std::size_t type(std::size_t car) const;
    const VehicleConfig& config(std::size_t car) const;
    const VehicleConfig& typeConfig(std::size_t type) const;
    std::size_t typeCount() const;
    // Lifetime identity survives address reuse; pose revisions also cover wheels
    // and explicit multiplayer poses, including while physics is not stepped.
    std::uint64_t instanceId() const;
    std::uint64_t poseRevision(std::size_t car) const;
    std::size_t count() const;
    // Driver occupancy controls detailed physics. After release, brake and
    // settle before returning to static boxes; passengers do not activate it.
    void setDriven(std::size_t car, bool driven);
    bool simulating(std::size_t car) const;
    const std::vector<std::size_t>& simulatedCars() const;
    VehiclePose pose(std::size_t car) const;
    // Replicated rigid poses for clients that do not step vehicle physics.
    void setPose(std::size_t car, const VehiclePose& pose, glm::vec3 velocity);
    glm::vec3 velocity(std::size_t car) const;
    glm::vec3 pointVelocity(std::size_t car, glm::vec3 point) const;
    void applyImpulse(std::size_t car, glm::vec3 impulse, glm::vec3 point);
    // Observe native contact resistance for next step's torque reserve, without
    // applying the contact impulse twice. Includes chassis and wheel contacts.
    void recordResistanceImpulse(std::size_t car, glm::vec3 impulse);
    std::optional<std::size_t> carForBody(b3BodyId body) const;
    // W/S throttle (+forward/-reverse), D/A steering (+right), Space brake.
    void drive(std::size_t car, float throttle, float steering, bool brake);
    void beforeStep(float seconds); // Once before each shared fixed physics step.
    std::array<WheelTraction, 4> wheelTraction(std::size_t car) const; // Last fixed-step sample, FL/FR/RL/RR.
    int groundedWheels(std::size_t car) const;
    bool overlaps(glm::vec3 minimum, glm::vec3 maximum) const;
    void appendContacts(glm::vec3 feet, float radius, float height, float skin,
                        std::vector<Contact>& contacts) const;
    std::optional<GroundHit> groundAt(float x, float z, float minY, float maxY) const;
    static Model geometry(); // Exactly one box and one 12-sided tire mesh.
    static constexpr float wheelRadius = .36f, wheelWidth = .24f;
    static constexpr float fullThrust = 19200; // N equivalent; each wheel gets 1/4 torque budget.
    static constexpr float normalThrust = 5500; // N; calibrated for the previous acceleration with the increased grip.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
