#include "physics_grab.hpp"
#include "physics_prop.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <cmath>
#include <stdexcept>

namespace viewer {
PhysicsGrab::PhysicsGrab(const PhysicsProp& prop, glm::vec3 localPoint, float maxForce)
    : world_(prop.world_), prop_(prop.body()) {
    const auto point = prop.pointState(localPoint).point;
    if (!std::isfinite(maxForce) || maxForce <= 0) throw std::invalid_argument("Invalid grab force");
    auto anchor = b3DefaultBodyDef();
    anchor.position = {point.x,point.y,point.z};
    anchor_ = b3CreateBody(world_->id(),&anchor);
    try {
        auto joint = b3DefaultMotorJointDef();
        joint.base.bodyIdA = anchor_;
        joint.base.bodyIdB = prop_;
        joint.base.localFrameB.p = {localPoint.x,localPoint.y,localPoint.z};
        joint.linearHertz = 4;
        joint.linearDampingRatio = 1; // Critical damping, solved at every physics substep.
        joint.maxSpringForce = maxForce;
        joint.maxVelocityTorque = maxForce*.02f; // Bounded hand friction; no orientation spring.
        joint_ = b3CreateMotorJoint(world_->id(),&joint);
        if (!b3Joint_IsValid(joint_)) throw std::runtime_error("Cannot create grab joint");
        world_->beginGrab(prop_);
        b3Joint_WakeBodies(joint_);
    } catch (...) {
        if (b3Body_IsValid(anchor_)) b3DestroyBody(anchor_);
        throw;
    }
}
PhysicsGrab::~PhysicsGrab() {
    world_->endGrab(prop_);
    // Destroying either attached body also destroys the joint.
    if (b3Body_IsValid(anchor_)) b3DestroyBody(anchor_);
}
bool PhysicsGrab::valid() const { return b3Joint_IsValid(joint_); }
void PhysicsGrab::setTarget(glm::vec3 point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        throw std::invalid_argument("Invalid grab target");
    if (!valid()) return;
    b3Body_SetTransform(anchor_,{point.x,point.y,point.z},b3Quat_identity);
    b3Joint_WakeBodies(joint_);
}
} // namespace viewer
