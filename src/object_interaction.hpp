#pragma once

#include "box_layer.hpp"
#include "camera.hpp"
#include "physics_prop.hpp"
#include "physics_grab.hpp"
#include <array>
#include <deque>

namespace viewer {
class CollisionWorld;
enum class Hand { Left, Right };

// Independent prop grips. The ordinary BoxLayer must outlive active grips.
// Input timestamps use one monotonic clock, in seconds, independent of dt.
class ObjectInteraction {
public:
    bool press(Hand hand, const Camera& camera, BoxLayer& boxes, const CollisionWorld& world, double seconds);
    void release(Hand hand, double seconds); // Recent mouse swing adds a throw impulse.
    void cancel(); // Mode/focus/cursor changes drop both grips without a throw boost.
    void cancel(Hand hand); // Replacing one grip leaves the other hand attached.
    bool grabbing() const;
    std::optional<std::size_t> heldBox(Hand hand) const;
    std::optional<PhysicsProp> heldProp(Hand hand) const;
    // Positive dy moves upward in world Y; dx follows the grab's camera right.
    void moveMouse(float dx, float dy, double seconds);
    void update(const Camera& camera, float dt);
    // World m/s from a sideways/upward mouse velocity. Only the upward
    // component adds forward speed; sideways/downward add none.
    static glm::vec3 throwVelocity(glm::vec2 swing, glm::vec3 right, glm::vec3 forward);
    static float rayReach(glm::vec3 direction); // Normalized camera ray: 1.5 m horizontal, 2 m vertical.
private:
    struct Motion { double seconds; glm::vec2 displacement; };
    struct Grab {
        BoxLayer* boxes;
        PhysicsProp prop;
        std::unique_ptr<PhysicsGrab> joint;
        glm::vec3 localPoint, eyeOffset, right, forward;
        glm::vec2 drag{0};
        std::deque<Motion> motion;
    };
    static std::size_t slot(Hand hand) { return hand == Hand::Left ? 0 : 1; }
    std::array<std::optional<Grab>, 2> grabs_;
};
} // namespace viewer
