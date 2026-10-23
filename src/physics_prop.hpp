#pragma once

#include <box3d/id.h>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>

namespace viewer {
class PhysicsWorld;
struct PropPointState { glm::vec3 point, velocity; }; // World metres and m/s.

// A grip borrows a body, keeping its world alive. Body generations prevent a
// deleted pickup/corpse prop from being confused with a newly allocated body.
class PhysicsProp {
public:
    PhysicsProp(std::shared_ptr<PhysicsWorld> world, b3BodyId body);
    bool valid() const;
    bool operator==(const PhysicsProp& other) const;
    glm::vec3 center() const;
    float mass() const;
    PropPointState pointState(glm::vec3 localPoint) const;
    void applyImpulse(glm::vec3 impulse, glm::vec3 point) const;
private:
    friend class PhysicsGrab;
    b3BodyId body() const;
    std::shared_ptr<PhysicsWorld> world_;
    b3BodyId body_;
};
struct PropRayHit {
    PhysicsProp prop;
    glm::vec3 point, localPoint;
    float distance;
};
// Shared broadphase query for crates, dropped weapons and loose torso contents.
std::optional<PropRayHit> raycastPhysicsProp(std::shared_ptr<PhysicsWorld> world,
    glm::vec3 origin, glm::vec3 direction, float reach);
} // namespace viewer
