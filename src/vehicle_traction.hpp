#pragma once

#include <box3d/id.h>

namespace viewer {
constexpr float rollingTireFriction = 3.f;
constexpr float slidingTireFriction = 2.f;

enum class WheelMotion { Airborne, Rolling, Sliding };
struct WheelTraction {
    WheelMotion motion = WheelMotion::Airborne;
    // Signed speeds relative to the supporting body, in meters per second.
    float forwardSpeed = 0;
    float longitudinalSlip = 0, lateralSlip = 0;
    float friction = rollingTireFriction;
};
// Samples support contacts, including wheel rotation and moving ground.
// Hysteresis keeps rolling/sliding changes stable near the slip threshold.
WheelTraction sampleWheelTraction(b3BodyId wheel, WheelMotion previous);
} // namespace viewer
