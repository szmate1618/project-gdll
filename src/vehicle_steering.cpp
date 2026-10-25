#include "vehicle_steering.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
float vehicleSteeringTarget(float input, float speed) {
    return -input * maxVehicleSteeringAngle / (1 + speed * .05f);
}

float advanceVehicleSteering(float current, float target, float seconds) {
    constexpr float steeringRate = 3.f, steeringReturnRate = 4.f; // rad/s
    const auto approach = [](float from, float to, float step) {
        return from + std::clamp(to - from, -step, step);
    };
    // Split reversals at center so their response is independent of step size.
    if (current * target < 0) {
        const float centeringTime = std::min(seconds, std::abs(current) / steeringReturnRate);
        current = approach(current, 0, steeringReturnRate * centeringTime);
        seconds -= centeringTime;
    }
    const float rate = std::abs(target) < std::abs(current) ? steeringReturnRate : steeringRate;
    return approach(current, target, rate * seconds);
}

std::array<float, 2> vehicleFrontSteeringAngles(const VehicleConfig& config, float centerAngle) {
    const auto front = (config.wheels[0] + config.wheels[1]) * .5f;
    const auto rear = (config.wheels[2] + config.wheels[3]) * .5f;
    const float wheelbase = rear.z - front.z;
    const float direction = centerAngle < 0 ? -1.f : 1.f;
    float curvature = std::abs(std::tan(centerAngle) / wheelbase);
    const float tangentLimit = std::tan(maxVehicleWheelSteeringAngle);
    for (std::size_t i = 0; i < 2; ++i) {
        const float offset = config.wheels[i].x - front.x;
        const float length = rear.z - config.wheels[i].z;
        const float bound = length - direction * offset * tangentLimit;
        if (bound > 0) curvature = std::min(curvature, tangentLimit / bound);
    }
    curvature *= direction;
    std::array<float, 2> angles{};
    for (std::size_t i = 0; i < 2; ++i) {
        const float offset = config.wheels[i].x - front.x;
        const float length = rear.z - config.wheels[i].z;
        angles[i] = std::atan2(length * curvature, 1 + offset * curvature);
    }
    return angles;
}
} // namespace viewer
