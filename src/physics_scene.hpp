#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace viewer {
class CollisionWorld;
class PhysicsWorld;
class BoxLayer;

// Lazily uploads scene triangles near moving props, with doorways cut by
// CollisionWorld, plus authoritative trunk capsules near grenades. Keeps mesh
// data alive until its Box3D shape is destroyed.
class PhysicsScene {
public:
    PhysicsScene(std::shared_ptr<PhysicsWorld> simulation, const CollisionWorld& collision,
                 glm::vec3 sceneMinimum, glm::vec3 sceneMaximum);
    ~PhysicsScene();
    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;
    void ensureNear(glm::vec3 minimum, glm::vec3 maximum);
    void ensureTrunksNear(glm::vec3 minimum, glm::vec3 maximum); // Grenade-only contacts.
    void prepare(const BoxLayer& boxes); // Before the shared physics step.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
