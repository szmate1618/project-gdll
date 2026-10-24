#pragma once

#include <box3d/id.h>

namespace viewer {
constexpr float rollingTireFriction = 3.f;
constexpr float slidingTireFriction = 2.f;
constexpr float lateralTireFriction = 2.2f;

enum class WheelMotion { Airborne, Rolling, Sliding };
struct WheelTraction {
    WheelMotion motion = WheelMotion::Airborne;
    // Signed speeds relative to the supporting body, in meters per second.
    float forwardSpeed = 0;
    float longitudinalSlip = 0, lateralSlip = 0;
    // Independent axes: a sideways skid does not reduce rolling-direction grip.
    bool longitudinalSliding = false, lateralSliding = false;
    float friction = rollingTireFriction;
    float lateralFriction = lateralTireFriction;
};
// Samples support contacts, including wheel rotation and moving ground.
// Hysteresis keeps rolling/sliding changes stable near the slip threshold.
WheelTraction sampleWheelTraction(b3BodyId wheel, const WheelTraction& previous = {});
// Compatibility for callers without per-axis history.
WheelTraction sampleWheelTraction(b3BodyId wheel, WheelMotion previous);
} // namespace viewer
