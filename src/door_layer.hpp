#pragma once

#include "door_geometry.hpp"
#include <memory>
#include <optional>

namespace viewer {
class PhysicsWorld;
struct DoorDrawPose { std::size_t draw; glm::mat4 transform; };
struct DoorCollider {
    glm::vec3 center, halfExtents;
    glm::quat rotation;
    std::size_t door = 0;
};
struct DoorRayHit {
    std::size_t door;
    glm::vec3 point, localPoint; // World meters and body-local meters, respectively.
    float distance;
};
struct DoorPointState { glm::vec3 point, velocity; }; // World meters and m/s.

// Owns door bodies and static hinge anchors in a shared simulation. Panels stay
// closed until activated by proximity or force; activated bodies can sleep.
class DoorLayer {
public:
    DoorLayer(std::vector<DoorPanel> panels, std::shared_ptr<PhysicsWorld> world);
    ~DoorLayer();
    DoorLayer(const DoorLayer&) = delete;
    DoorLayer& operator=(const DoorLayer&) = delete;
    std::size_t count() const;
    std::size_t activeCount() const;
    const DoorPanel& panel(std::size_t door) const; // Immutable closed geometry; stable door index.
    void activateNear(glm::vec3 position, float radius = 32);
    // World-space forces in N, impulses in N*s, application points in meters.
    // Force applies to the next physics step; continuous force must be reapplied.
    void applyForce(std::size_t door, glm::vec3 force, glm::vec3 point);
    void applyImpulse(std::size_t door, glm::vec3 impulse, glm::vec3 point);
    float angle(std::size_t door) const; // Radians: zero closed, positive outward.
    glm::mat4 transform(std::size_t door) const;
    // Read after PhysicsWorld::step(). Only active panels need renderer updates.
    std::vector<DoorDrawPose> poses() const;
    // Conservative spatial query over hinge sweeps. Returns current Box3D body
    // poses, or the identical closed box for panels not yet activated.
    std::vector<DoorCollider> colliders(glm::vec3 minimum, glm::vec3 maximum) const;
    // Nearest current panel box on a finite ray. Does not activate bodies.
    std::optional<DoorRayHit> raycast(glm::vec3 origin, glm::vec3 direction, float reach) const;
    DoorPointState pointState(std::size_t door, glm::vec3 localPoint) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
