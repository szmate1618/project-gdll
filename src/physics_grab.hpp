#pragma once

#include <box3d/id.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace viewer {
class PhysicsWorld;
class PhysicsProp;

// Owns one hand anchor and a damped point constraint with bounded rotational friction.
// Explicit rotation adds an angular spring with bounded hand torque.
// Destroying the grabbed body invalidates the joint; releasing removes the anchor.
class PhysicsGrab {
public:
    PhysicsGrab(const PhysicsProp& prop, glm::vec3 localPoint, float maxForce);
    ~PhysicsGrab();
    PhysicsGrab(const PhysicsGrab&) = delete;
    PhysicsGrab& operator=(const PhysicsGrab&) = delete;
    bool valid() const;
    void setTarget(glm::vec3 point); // World metres.
    glm::quat rotation() const; // Current prop orientation in world space.
    void setRotationTarget(glm::quat rotation);
    void setCraftingMode(bool enabled);
    void park(); // Keep this pose after the hand releases/replaces its grip.
    void resume();
    void setGripPoint(glm::vec3 localPoint) { localPoint_ = localPoint; }
private:
    std::shared_ptr<PhysicsWorld> world_;
    b3BodyId prop_, anchor_{};
    b3JointId joint_{};
    glm::quat rotation_{1, 0, 0, 0};
    float maxTorque_;
    glm::vec3 localPoint_{0};
    bool crafting_ = false;
};
} // namespace viewer
