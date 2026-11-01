#include "physics_attachment.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <cmath>
#include <stdexcept>

namespace viewer {
PhysicsAttachment::PhysicsAttachment(std::shared_ptr<PhysicsWorld> world, b3BodyId body)
    : world_(std::move(world)), body_(body) {
    if (!world_ || !b3Body_IsValid(body_) || world_->isGrabbed(body_))
        throw std::invalid_argument("Attachment requires an available physics body");
    auto anchor = b3DefaultBodyDef();
    anchor.position = b3Body_GetPosition(body_);
    anchor_ = b3CreateBody(world_->id(),&anchor);
    try {
        auto joint = b3DefaultSphericalJointDef();
        joint.base.bodyIdA = anchor_;
        joint.base.bodyIdB = body_;
        joint_ = b3CreateSphericalJoint(world_->id(),&joint);
        if (!b3Joint_IsValid(joint_)) throw std::runtime_error("Cannot attach physics body");
        world_->beginGrab(body_);
        b3Joint_WakeBodies(joint_);
    } catch (...) {
        if (b3Body_IsValid(anchor_)) b3DestroyBody(anchor_);
        throw;
    }
}
PhysicsAttachment::~PhysicsAttachment() {
    world_->endGrab(body_);
    if (b3Body_IsValid(anchor_)) b3DestroyBody(anchor_);
}
bool PhysicsAttachment::valid() const { return b3Joint_IsValid(joint_); }
void PhysicsAttachment::setTarget(glm::vec3 point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        throw std::invalid_argument("Attachment target must be finite");
    if (!valid()) return;
    b3Body_SetTransform(anchor_,{point.x,point.y,point.z},b3Quat_identity);
    b3Joint_WakeBodies(joint_);
}
} // namespace viewer
