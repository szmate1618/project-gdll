#include "door_slam.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
std::optional<DoorSlamEvent> sampleDoorSlam(DoorSlamState& state, float seconds, float radians, glm::vec3 position) {
    if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(radians)) return {};
    const float angle = std::abs(radians);
    if (!state.initialized) {
        state.initialized = true; state.angle = angle; state.armed = angle >= .12f;
        return {};
    }
    state.cooldown = std::max(0.f, state.cooldown - seconds);
    const float closingSpeed = (state.angle - angle) / seconds;
    std::optional<DoorSlamEvent> event;
    if (angle >= .12f) state.armed = true;
    if (angle <= .035f && state.angle > .035f) {
        // The solver can stop a door within one sample. Retain its incoming
        // speed instead of measuring only the clamped final displacement.
        const float speed = std::max(closingSpeed, state.closingSpeed);
        if (state.armed && state.cooldown == 0 && speed >= .8f) {
            const float db = std::min(70.f, 60.f + 20.f * std::log10(speed / 2.f));
            event = DoorSlamEvent{position, speed, db, std::min(1.f, .8f * speed / 2.f)};
            state.cooldown = .4f;
        }
        state.armed = false;
    }
    state.angle = angle; state.closingSpeed = closingSpeed;
    return event;
}
}
