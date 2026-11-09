#pragma once

#include "door_interaction.hpp"
#include "object_interaction.hpp"

namespace viewer {
// Object handling: hold left to drag and release to drop; hold right to rotate.
// The throw action charges a throw/push, automatically released after two seconds.
// Call cancel when unequipped or input is suspended.
// Input timestamps use one monotonic clock in seconds, independent of dt.
class PlayerInteraction {
public:
    bool grabbing() const { return doors_.grabbing() || objects_.grabbing(); }
    bool charging() const { return chargeStarted_.has_value(); }
    bool rotating() const { return objects_.rotating(); }
    std::optional<std::size_t> grabbedDoor() const { return doors_.grabbedDoor(); }
    std::optional<std::size_t> heldBox() const { return objects_.heldBox(); }
    std::optional<PhysicsProp> heldProp() const { return objects_.heldProp(); }
    bool press(Hand hand, const Camera& camera, DoorLayer& doors, BoxLayer& boxes,
               const CollisionWorld& world, double seconds);
    void release(Hand hand, const Camera& camera, DoorLayer& doors, BoxLayer& boxes,
                 const CollisionWorld& world, double seconds);
    void cancel();
    bool grip(const Camera& camera, BoxLayer& boxes, const PhysicsProp& prop) {
        cancel();
        return objects_.grip(camera, boxes, prop);
    }
    void look(Camera& camera, float dx, float dy);
    void moveGrip(float dx, float dy);
    void update(const Camera& camera, float dt, DoorLayer& doors, BoxLayer& boxes,
                const CollisionWorld& world, double seconds);
private:
    DoorInteraction doors_;
    ObjectInteraction objects_;
    std::optional<double> chargeStarted_;
    bool chargingGrip_ = false;
};
} // namespace viewer
