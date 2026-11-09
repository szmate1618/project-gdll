#pragma once

#include "box_layer.hpp"
#include "camera.hpp"
#include "physics_prop.hpp"
#include "physics_grab.hpp"

namespace viewer {
class CollisionWorld;
enum class Hand { Left, Right, Throw }; // Grip, rotate, and charged throw/push actions.

// One persistent prop grip. The ordinary BoxLayer must outlive the grip.
class ObjectInteraction {
public:
    bool press(const Camera& camera, BoxLayer& boxes, const CollisionWorld& world);
    bool grip(const Camera& camera, BoxLayer& boxes, const PhysicsProp& prop);
    void cancel(); // Drops without adding momentum.
    bool grabbing() const { return heldProp().has_value(); }
    std::optional<std::size_t> heldBox() const;
    std::optional<PhysicsProp> heldProp() const;
    // Positive dy moves upward in world Y; dx follows the grab's camera right.
    void moveMouse(float dx, float dy);
    bool beginRotation(const Camera& camera);
    void endRotation();
    bool rotating() const;
    void rotateMouse(float dx, float dy);
    void update(const Camera& camera, float dt);
    static float rayReach(glm::vec3 direction); // Normalized ray: 1.5 m horizontal, 2 m vertical.
private:
    struct Rotation {
        glm::quat initial;
        glm::vec3 right;
        float yaw = 0, pitch = 0; // Radians relative to the start of this drag.
    };
    struct Grab {
        BoxLayer* boxes;
        PhysicsProp prop;
        std::unique_ptr<PhysicsGrab> joint;
        glm::vec3 localPoint, eyeOffset, right;
        glm::vec2 drag{0};
        std::optional<Rotation> rotation;
    };
    std::optional<Grab> grab_;
};
} // namespace viewer
