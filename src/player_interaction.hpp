#pragma once

#include "door_interaction.hpp"
#include "object_interaction.hpp"

namespace viewer {
// Routes input between the existing door gesture and independent prop hands.
class PlayerInteraction {
public:
    ControlMode mode() const { return doors_.mode(); }
    const char* modeName() const { return doors_.modeName(); }
    void toggleMode();
    bool grabbing() const { return doors_.grabbing() || objects_.grabbing(); }
    std::optional<std::size_t> grabbedDoor() const { return doors_.grabbedDoor(); }
    std::optional<std::size_t> heldBox(Hand hand) const { return objects_.heldBox(hand); }
    bool press(Hand hand, const Camera& camera, DoorLayer& doors, BoxLayer& boxes,
               const CollisionWorld& world, double seconds);
    void release(Hand hand, double seconds);
    void cancel();
    void look(Camera& camera, float dx, float dy, double seconds);
    void update(const Camera& camera, float dt);
private:
    DoorInteraction doors_;
    ObjectInteraction objects_;
};
} // namespace viewer
