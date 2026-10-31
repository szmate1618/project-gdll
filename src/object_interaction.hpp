#pragma once

#include "box_layer.hpp"
#include "camera.hpp"
#include "physics_prop.hpp"
#include "physics_grab.hpp"

namespace viewer {
class CollisionWorld;
enum class Hand { Left, Right }; // Mouse buttons for the object-handling skill.

// One persistent prop grip. The ordinary BoxLayer must outlive the grip.
class ObjectInteraction {
public:
    bool press(const Camera& camera, BoxLayer& boxes, const CollisionWorld& world);
    void cancel(); // Drops without adding momentum.
    bool grabbing() const { return heldProp().has_value(); }
    std::optional<std::size_t> heldBox() const;
    std::optional<PhysicsProp> heldProp() const;
    // Positive dy moves upward in world Y; dx follows the grab's camera right.
    void moveMouse(float dx, float dy);
    void update(const Camera& camera, float dt);
    static float rayReach(glm::vec3 direction); // Normalized ray: 1.5 m horizontal, 2 m vertical.
private:
    struct Grab {
        BoxLayer* boxes;
        PhysicsProp prop;
        std::unique_ptr<PhysicsGrab> joint;
        glm::vec3 localPoint, eyeOffset, right;
        glm::vec2 drag{0};
    };
    std::optional<Grab> grab_;
};
} // namespace viewer
