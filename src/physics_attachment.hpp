#pragma once

#include <box3d/id.h>
#include <glm/vec3.hpp>
#include <memory>

namespace viewer {
class PhysicsWorld;
// A firm position constraint at a body's center, with free rotation. Owns the
// moving anchor and exclusive carry claim; body removal invalidates the joint.
class PhysicsAttachment {
public:
    PhysicsAttachment(std::shared_ptr<PhysicsWorld> world, b3BodyId body);
    ~PhysicsAttachment();
    PhysicsAttachment(const PhysicsAttachment&) = delete;
    PhysicsAttachment& operator=(const PhysicsAttachment&) = delete;
    bool valid() const;
    void setTarget(glm::vec3 point); // World meters.
private:
    std::shared_ptr<PhysicsWorld> world_;
    b3BodyId body_{}, anchor_{};
    b3JointId joint_{};
};
} // namespace viewer
