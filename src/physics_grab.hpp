#pragma once

#include <box3d/id.h>
#include <glm/vec3.hpp>
#include <memory>

namespace viewer {
class PhysicsWorld;
class PhysicsProp;

// Owns one hand anchor and a damped point constraint with bounded rotational friction.
// Destroying the grabbed body invalidates the joint; releasing removes the anchor.
class PhysicsGrab {
public:
    PhysicsGrab(const PhysicsProp& prop, glm::vec3 localPoint, float maxForce);
    ~PhysicsGrab();
    PhysicsGrab(const PhysicsGrab&) = delete;
    PhysicsGrab& operator=(const PhysicsGrab&) = delete;
    bool valid() const;
    void setTarget(glm::vec3 point); // World metres.
private:
    std::shared_ptr<PhysicsWorld> world_;
    b3BodyId prop_, anchor_{};
    b3JointId joint_{};
};
} // namespace viewer
