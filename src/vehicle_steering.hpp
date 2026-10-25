#pragma once

#include "vehicle_config.hpp"
#include <array>

namespace viewer {
constexpr float maxVehicleSteeringAngle = .5235988f; // 30 degrees at the axle midpoint.
constexpr float maxVehicleWheelSteeringAngle = .7853982f; // 45-degree physical wheel limit.

// Radians in the wheel joint's frame: negative angles steer right.
float vehicleSteeringTarget(float input, float speed);
float advanceVehicleSteering(float current, float target, float seconds);

// Front-left/front-right targets share a rear-axle turning center. If needed,
// reduce curvature for both wheels so the inner wheel stays within its limit.
std::array<float, 2> vehicleFrontSteeringAngles(const VehicleConfig& config, float centerAngle);
} // namespace viewer
