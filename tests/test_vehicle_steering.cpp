#include "vehicle_steering.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < 1e-5f, message);
}

// Intersect the wheel's axle line with the rear-axle row. A rolling tire's
// velocity is perpendicular to that line, so this is its implied turn center.
float turningCenter(const glm::vec3& wheel, float angle, float rearZ) {
    const glm::vec2 axle(std::cos(angle), -std::sin(angle));
    return wheel.x + (rearZ - wheel.z) * axle.x / axle.y;
}

void ackermann(const viewer::VehicleConfig& config, bool reachesWheelLimit) {
    const auto straight = viewer::vehicleFrontSteeringAngles(config, 0);
    near(straight[0], 0, "Straight input must leave the left wheel straight");
    near(straight[1], 0, "Straight input must leave the right wheel straight");
    for (float center : {.08f, .25f, viewer::maxVehicleSteeringAngle}) {
        const auto right = viewer::vehicleFrontSteeringAngles(config, -center);
        const auto left = viewer::vehicleFrontSteeringAngles(config, center);
        require(right[0] < 0 && right[1] < 0 && left[0] > 0 && left[1] > 0,
                "Both front wheels must steer toward the requested side");
        if (config.wheels[0].z == config.wheels[1].z) {
            require(right[1] < right[0], "Right turns must use greater right-wheel lock");
            require(left[0] > left[1], "Left turns must use greater left-wheel lock");
        }
        const float rearZ = (config.wheels[2].z + config.wheels[3].z) * .5f;
        for (const auto& angles : {right, left}) {
            near(turningCenter(config.wheels[0], angles[0], rearZ),
                 turningCenter(config.wheels[1], angles[1], rearZ),
                 "Both rolling directions must share one turning center on the rear axle");
            require(std::abs(angles[0]) <= viewer::maxVehicleWheelSteeringAngle + 1e-6f &&
                    std::abs(angles[1]) <= viewer::maxVehicleWheelSteeringAngle + 1e-6f,
                    "Ackermann correction must respect both physical steering limits");
        }
        require(turningCenter(config.wheels[0], right[0], rearZ) > config.wheels[1].x &&
                turningCenter(config.wheels[0], left[0], rearZ) < config.wheels[0].x,
                "Turn centers must lie on the requested side of the car");
    }
    if (reachesWheelLimit) {
        const auto lock = viewer::vehicleFrontSteeringAngles(config, -viewer::maxVehicleSteeringAngle);
        near(lock[1], -viewer::maxVehicleWheelSteeringAngle,
             "A short wheelbase must reduce common curvature at the inner wheel limit");
    }
}

void geometry() {
    viewer::VehicleConfig config;
    ackermann(config, false);
    const auto right = viewer::vehicleFrontSteeringAngles(config, -.3f);
    const auto left = viewer::vehicleFrontSteeringAngles(config, .3f);
    near(right[0], -left[1], "Symmetric left and right outer wheels must mirror");
    near(right[1], -left[0], "Symmetric left and right inner wheels must mirror");
    auto known = config;
    known.wheels = {{{-.5f, 0, -1}, {.5f, 0, -1}, {-.5f, 0, 1}, {.5f, 0, 1}}};
    const auto knownAngles = viewer::vehicleFrontSteeringAngles(known, -std::atan(.5f));
    near(turningCenter(known.wheels[0], knownAngles[0], 1), 4,
         "A two-meter wheelbase at the requested center angle must retain a four-meter radius");
    // A wheelbase shorter than its track needs a curvature cap at full lock.
    for (std::size_t i = 0; i < 4; ++i) config.wheels[i].z = i < 2 ? -.3f : .3f;
    ackermann(config, true);
    // Off-center axles and a staggered front row must still share an ICR.
    config.wheels[0].x = -.45f; config.wheels[1].x = 1.25f;
    config.wheels[0].z = -.4f; config.wheels[1].z = -.25f;
    ackermann(config, true);
}

void targetsAndResponse() {
    near(viewer::vehicleSteeringTarget(1, 0), -viewer::maxVehicleSteeringAngle,
         "D must request full right lock at rest");
    near(viewer::vehicleSteeringTarget(-1, 20), viewer::maxVehicleSteeringAngle * .5f,
         "Steering lock must retain the existing speed reduction");
    near(viewer::vehicleSteeringTarget(0, 15), 0, "Released steering must target center");
    near(viewer::advanceVehicleSteering(0, .5f, .1f), .3f, "Turn-in rate must remain 3 rad/s");
    near(viewer::advanceVehicleSteering(.5f, 0, .1f), .1f, "Centering rate must remain 4 rad/s");
    near(viewer::advanceVehicleSteering(.2f, -.5f, .1f), -.15f,
         "Reversals must use the centering rate until crossing zero");
    const auto oneStep = viewer::advanceVehicleSteering(.2f, -.5f, .1f);
    auto split = .2f;
    for (int i = 0; i < 10; ++i) split = viewer::advanceVehicleSteering(split, -.5f, .01f);
    near(split, oneStep, "Steering reversals must be independent of timestep partitioning");
    near(viewer::advanceVehicleSteering(.1f, .2f, 1), .2f, "Steering must not overshoot its target");
}
} // namespace

int main() {
    try { geometry(); targetsAndResponse(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
