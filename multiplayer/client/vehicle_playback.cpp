#include "vehicle_playback.hpp"
#include "vehicle_layer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
namespace {
constexpr double extrapolationSeconds = .25; // Bound unconfirmed travel after packet loss.
constexpr float maximumAngularSpeed = 6.2831853f; // rad/s; larger jumps reset angular prediction.

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid(const RigidPose& pose) {
    const float length = glm::length(pose.rotation);
    return finite(pose.center) && std::isfinite(length) && std::abs(length - 1) <= .001f;
}

glm::vec3 angularVelocity(glm::quat before, glm::quat after, double seconds) {
    if (seconds <= 0 || seconds > extrapolationSeconds) return {};
    // World-space rotation taking the previous chassis to the latest one.
    // q and -q describe the same orientation; always use the shorter arc.
    auto turn = glm::normalize(after * glm::conjugate(glm::normalize(before)));
    if (turn.w < 0) turn = -turn;
    const auto axis = glm::vec3(turn.x, turn.y, turn.z);
    const float sine = glm::length(axis);
    if (sine < 1e-6f) return {};
    const float speed = 2 * std::atan2(sine, turn.w) / static_cast<float>(seconds);
    if (speed > maximumAngularSpeed) return {};
    return axis * (speed / sine);
}
} // namespace

void VehiclePlayback::push(std::uint64_t tick, const std::vector<VehicleState>& updates) {
    if (latestTick_ && tick <= *latestTick_) return;
    if (updates.size() > maxVehicles) throw std::runtime_error("Vehicle update batch exceeds capacity");
    std::array<bool, maxVehicles> seen{};
    std::size_t count = cars_.size();
    // Validate the complete batch before publishing states or accepting its
    // global tick. New IDs must fill the dense population, in any wire order.
    for (const auto& state : updates) {
        if (state.id >= maxVehicles || state.type >= viewer::maxVehicleTypes || seen[state.id])
            throw std::runtime_error("Invalid or duplicate vehicle ID");
        seen[state.id] = true;
        if (state.id < cars_.size() && cars_[state.id].latest.type != state.type)
            throw std::runtime_error("Vehicle type changed during session");
        if (!finite(state.velocity) || !valid(state.chassis) ||
            !std::all_of(state.wheels.begin(), state.wheels.end(), [](const auto& wheel) { return valid(wheel); }))
            throw std::runtime_error("Invalid replicated vehicle pose");
        count = std::max(count, std::size_t(state.id) + 1);
    }
    for (auto id = cars_.size(); id < count; ++id)
        if (!seen[id]) throw std::runtime_error("Server supplied non-dense vehicle IDs");

    const auto previousCount = cars_.size();
    cars_.resize(count);
    for (const auto& state : updates) {
        auto& replica = cars_[state.id];
        // Every intervening complete snapshot confirmed an omitted car was
        // unchanged. Measure resumed rotation from the last such confirmation.
        replica.angularVelocity = state.id >= previousCount ? glm::vec3(0) : angularVelocity(
            replica.latest.chassis.rotation, state.chassis.rotation, double(tick - *latestTick_) / 60);
        replica.latest = state;
        replica.tick = tick;
        replica.appliedAge.reset();
    }
    latestTick_ = tick;
}

void VehiclePlayback::apply(viewer::VehicleLayer& vehicles, double serverTick) {
    if (!std::isfinite(serverTick) || serverTick < 0) throw std::invalid_argument("Invalid vehicle prediction time");
    if (vehicles.count() > cars_.size()) throw std::runtime_error("Client vehicle population exceeds server state");
    // Check every type before creating or moving any car in the layer.
    for (std::size_t id = 0; id < cars_.size(); ++id) {
        const auto type = cars_[id].latest.type;
        if (type >= vehicles.typeCount() || (id < vehicles.count() && vehicles.type(id) != type))
            throw std::runtime_error("Replicated vehicle type does not match session assets");
    }
    for (auto id = vehicles.count(); id < cars_.size(); ++id) {
        const auto& state = cars_[id].latest;
        vehicles.add(state.chassis.center, state.chassis.rotation, state.type);
        cars_[id].appliedAge.reset();
    }
    for (std::size_t id = 0; id < cars_.size(); ++id) {
        auto& replica = cars_[id];
        const auto& state = replica.latest;
        const float turnSpeed = glm::length(replica.angularVelocity);
        // Omission in a newer complete snapshot confirms no further rotation.
        const bool turning = replica.tick == *latestTick_ && turnSpeed > 1e-6f;
        const double age = state.velocity == glm::vec3(0) && !turning ? 0 :
            std::clamp((serverTick - double(replica.tick)) / 60, 0.0, extrapolationSeconds);
        if (replica.appliedAge == age && replica.appliedTurning == turning) continue;
        const auto turn = turning ? glm::angleAxis(turnSpeed * static_cast<float>(age),
                                                   replica.angularVelocity / turnSpeed) : glm::quat(1, 0, 0, 0);
        auto pose = vehicles.pose(id);
        pose.chassis.center = state.chassis.center + state.velocity * static_cast<float>(age);
        pose.chassis.rotation = glm::normalize(turn * state.chassis.rotation);
        for (std::size_t wheel = 0; wheel < pose.wheels.size(); ++wheel)
            pose.wheels[wheel] = glm::translate(glm::mat4(1), pose.chassis.center +
                turn * (state.wheels[wheel].center - state.chassis.center)) *
                glm::mat4_cast(glm::normalize(turn * state.wheels[wheel].rotation));
        vehicles.setPose(id, pose, state.velocity);
        replica.appliedAge = age;
        replica.appliedTurning = turning;
    }
}
} // namespace multiplayer
