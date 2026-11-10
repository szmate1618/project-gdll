#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace viewer {
class Camera;
class PhysicsProp;

// The hand borrows this world-owned service; ordinary prop handling has no
// inventory dependency. Implementations own nail previews and fastening joints.
class NailInteraction {
public:
    virtual ~NailInteraction() = default;
    virtual bool isNail(const PhysicsProp& prop) const = 0;
    virtual bool snap(const PhysicsProp& prop, const Camera& rayCamera) = 0;
    virtual bool pinned(const PhysicsProp& prop) const = 0;
    virtual glm::vec3 tip(const PhysicsProp& prop) const = 0;
    virtual void rotate(const PhysicsProp& prop, glm::quat rotation) = 0;
    virtual bool drive(const PhysicsProp& prop) = 0;
    virtual void detach(const PhysicsProp& prop) = 0;
};
}
