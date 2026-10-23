#pragma once

#include "model.hpp"
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace viewer {
class PhysicsWorld;
struct Contact;
struct GroundHit;
struct BoxPose {
    std::size_t index;
    glm::vec3 center, halfExtents;
    glm::quat rotation;
    glm::mat4 transform() const;
};
struct BoxRayHit {
    std::size_t box;
    glm::vec3 point, localPoint; // World and body-local meters.
    float distance;
};
struct BoxPointState { glm::vec3 point, velocity; }; // World meters and m/s.
struct BoxSpawnMotion {
    glm::quat rotation{1,0,0,0};
    glm::vec3 velocity{0}, angularVelocity{0}; // World m/s and rad/s.
    float mass = 0; // kg; zero retains the crate mass estimate.
    bool continuous = false;
};

// Owns dynamic untextured props in the shared Y-up, meter-based simulation.
// Collision queries are read-only; pushing is an explicit dt-scaled action.
class BoxLayer {
public:
    explicit BoxLayer(std::shared_ptr<PhysicsWorld> world);
    ~BoxLayer();
    BoxLayer(const BoxLayer&) = delete;
    BoxLayer& operator=(const BoxLayer&) = delete;
    std::size_t add(glm::vec3 center, glm::vec3 size, const BoxSpawnMotion& motion = {});
    // Dense removal moves the last box into this slot. Owners must update their
    // matching records and release any grips referencing these two indices.
    void remove(std::size_t index);
    std::size_t count() const;
    std::uint64_t revision() const;
    BoxPose pose(std::size_t index) const;
    // Apply a server pose without advancing local physics. Size is immutable.
    void setPose(std::size_t index, glm::vec3 center, glm::quat rotation);
    glm::vec3 velocity(std::size_t index) const; // World meters per second.
    float mass(std::size_t index) const; // Kilograms.
    BoxPointState pointState(std::size_t index, glm::vec3 localPoint) const;
    void applyImpulse(std::size_t index, glm::vec3 impulse, glm::vec3 point); // N*s, world meters.
    std::optional<BoxRayHit> raycast(glm::vec3 origin, glm::vec3 direction, float reach) const;
    std::vector<BoxPose> query(glm::vec3 minimum, glm::vec3 maximum) const;
    void appendContacts(glm::vec3 feet, float radius, float height, float skin,
                        std::vector<Contact>& contacts) const;
    std::optional<GroundHit> groundAt(float x, float z, float minY, float maxY) const;
    bool blocksSight(glm::vec3 origin, glm::vec3 target) const;
    void push(glm::vec3 feet, float radius, float height, glm::vec3 velocity, float dt);
    std::vector<glm::vec3> debugLines(glm::vec3 minimum, glm::vec3 maximum) const;
    // Shared face-normal cube, centered at the origin with full side length 1.
    static Model unitCube();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
