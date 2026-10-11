#pragma once

#include "camera.hpp"
#include "door_layer.hpp"
#include <optional>

namespace viewer {
class CollisionWorld;
enum class ControlMode { Combat, Interaction };

// Input-independent door gesture. The borrowed door layer must outlive a grab.
class DoorInteraction {
public:
    ControlMode mode() const { return mode_; }
    const char* modeName() const { return mode_ == ControlMode::Combat ? "Combat" : "Interaction"; }
    void toggleMode();
    bool grabbing() const { return grab_.has_value(); }
    bool press(const Camera& camera, DoorLayer& doors, const CollisionWorld& world);
    void release() { grab_.reset(); }
    // Pixels, positive dy upwards: drag right/left or push forward/pull back.
    // Outside a successful grab this routes to ordinary camera look.
    void look(Camera& camera, float dx, float dy);
    // Call after player movement. Integrates a bounded horizontal spring force
    // as a dt-scaled impulse, including displacement from walking/strafe.
    void update(const Camera& camera, float dt);
private:
    struct Grab {
        DoorLayer* doors;
        std::size_t door;
        glm::vec3 localPoint, eyeOffset, forward, right;
        glm::vec2 drag{0}; // Horizontal/depth offset in meters.
    };
    ControlMode mode_ = ControlMode::Combat;
    std::optional<Grab> grab_;
};
} // namespace viewer
