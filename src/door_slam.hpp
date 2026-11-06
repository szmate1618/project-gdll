#pragma once
#include <glm/vec3.hpp>
#include <optional>

namespace viewer {
struct DoorSlamState {
    float angle = 0, closingSpeed = 0, cooldown = 0;
    bool initialized = false, armed = false;
};
struct DoorSlamEvent {
    glm::vec3 position{0};
    float speed = 0, decibels = 0, gain = 0;
};
// Shared closure detection for authoritative physics and positional playback.
// Angles are hinge radians; speed is rad/s immediately before the closed stop.
std::optional<DoorSlamEvent> sampleDoorSlam(DoorSlamState& state, float seconds, float angle, glm::vec3 position);
}
