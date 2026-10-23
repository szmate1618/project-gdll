#pragma once
#include "zombie_wounds.hpp"
#include <memory>

namespace viewer {
class PhysicsWorld;
class PhysicsScene;
// Owns hollow kinematic cavities and their three loose demonstration props.
// Geometry is rebuilt on damage; normal movement changes only the rigid frame.
class ZombieTorsoPhysics {
public:
    explicit ZombieTorsoPhysics(std::shared_ptr<PhysicsWorld> world);
    ~ZombieTorsoPhysics();
    void wound(std::size_t id, const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
               const ZombieTorsoWound& wound, const glm::mat4& frame, glm::vec3 velocity);
    void move(std::size_t id, const glm::mat4& frame, float seconds);
    void prepare(PhysicsScene* scene);
    void remove(std::size_t id);
    std::vector<std::size_t> ids() const;
    std::vector<glm::mat4> contents() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
