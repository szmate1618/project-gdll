#pragma once

#include "box_layer.hpp"
#include <array>

namespace viewer {
// Runtime meters, Y up; the car's local forward is -Z, wheel axle is X.
// Owns five rigid bodies per car. The shared world must outlive this layer.
struct VehiclePose {
    BoxPose chassis;
    std::array<glm::mat4, 4> wheels;
};
class VehicleLayer {
public:
    explicit VehicleLayer(std::shared_ptr<PhysicsWorld> world);
    ~VehicleLayer();
    VehicleLayer(const VehicleLayer&) = delete;
    VehicleLayer& operator=(const VehicleLayer&) = delete;
    std::size_t add(glm::vec3 center, float yawRadians = 0);
    std::size_t count() const;
    VehiclePose pose(std::size_t car) const;
    glm::vec3 velocity(std::size_t car) const;
    glm::vec3 pointVelocity(std::size_t car, glm::vec3 point) const;
    void applyImpulse(std::size_t car, glm::vec3 impulse, glm::vec3 point);
    // W/S throttle (+forward/-reverse), D/A steering (+right), Space brake.
    void drive(std::size_t car, float throttle, float steering, bool brake);
    void beforeStep(float seconds); // Once before each shared fixed physics step.
    int groundedWheels(std::size_t car) const;
    void appendContacts(glm::vec3 feet, float radius, float height, float skin,
                        std::vector<Contact>& contacts) const;
    std::optional<GroundHit> groundAt(float x, float z, float minY, float maxY) const;
    static Model geometry(); // Exactly one box and one 12-sided tire mesh.
    static constexpr float wheelRadius = .36f, wheelWidth = .24f;
    static constexpr float fullThrust = 6400; // N; each wheel gets 1/4 torque budget.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
