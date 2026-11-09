#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace viewer {
class CollisionWorld;
class PhysicsWorld;
class BoxLayer;

// Shared scene collision in runtime meters. Ground uses resident 100 m sections;
// other static geometry uses smaller tiles, with doorways cut by CollisionWorld.
// Mesh data outlives its Box3D shape. Prepared ground remains until destruction.
class PhysicsScene {
public:
    static constexpr float groundSectionSize = 100.f;
    PhysicsScene(std::shared_ptr<PhysicsWorld> simulation, const CollisionWorld& collision,
                 glm::vec3 sceneMinimum, glm::vec3 sceneMaximum);
    ~PhysicsScene();
    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;
    // Preconstruct shared terrain/road colliders. Selection uses only X/Z;
    // request Y still must be finite and ordered. No vegetation is imported.
    void ensureGroundNear(glm::vec3 minimum, glm::vec3 maximum);
    // Prepare the center's section and nearby sections before play/movement.
    void prewarmGround(glm::vec3 center, float range = groundSectionSize);
    void ensureNear(glm::vec3 minimum, glm::vec3 maximum);
    void prepare(const BoxLayer& boxes, float padding = 4); // Metres; before the shared physics step.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
