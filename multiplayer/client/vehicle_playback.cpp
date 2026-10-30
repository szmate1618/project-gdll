#include "vehicle_playback.hpp"
#include "vehicle_layer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
namespace {
constexpr std::size_t historySize = 32;
constexpr double extrapolationSeconds = .25; // Bound unconfirmed travel after packet loss.
constexpr float maximumAngularSpeed = 6.2831853f; // rad/s; larger jumps reset angular prediction.

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid(const RigidPose& pose) {
    const float length = glm::length(pose.rotation);
    return finite(pose.center) && std::isfinite(length) && std::abs(length - 1) <= .001f;
}

RigidPose blend(const RigidPose& before, const RigidPose& after, float t) {
    return {glm::mix(before.center, after.center, t), glm::slerp(before.rotation, after.rotation, t)};
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
    // Validate the complete batch before publishing histories or accepting its
    // global tick. New IDs must fill the dense population, in any wire order.
    for (const auto& state : updates) {
        if (state.id >= maxVehicles || state.type >= viewer::maxVehicleTypes || seen[state.id])
            throw std::runtime_error("Invalid or duplicate vehicle ID");
        seen[state.id] = true;
        if (state.id < cars_.size() && cars_[state.id].samples.back().state.type != state.type)
            throw std::runtime_error("Vehicle type changed during session");
        if (!finite(state.velocity) || !valid(state.chassis) ||
            !std::all_of(state.wheels.begin(), state.wheels.end(), [](const auto& wheel) { return valid(wheel); }))
            throw std::runtime_error("Invalid replicated vehicle pose");
        count = std::max(count, std::size_t(state.id) + 1);
    }
    for (auto id = cars_.size(); id < count; ++id)
        if (!seen[id]) throw std::runtime_error("Server supplied non-dense vehicle IDs");

    cars_.resize(count);
    for (const auto& state : updates) {
        auto& history = cars_[state.id];
        auto& samples = history.samples;
        // An omitted car was unchanged at the preceding global snapshot. Add
        // that hold only when it moves again, so a long parked interval never
        // stretches its first movement across the entire silent history.
        if (!samples.empty() && latestTick_ && samples.back().tick < *latestTick_)
            samples.push_back({*latestTick_, samples.back().state});
        history.angularVelocity = samples.empty() ? glm::vec3(0) : angularVelocity(
            samples.back().state.chassis.rotation, state.chassis.rotation,
            double(tick - samples.back().tick) / 60);
        samples.push_back({tick, state});
        while (samples.size() > historySize) samples.pop_front();
    }
    latestTick_ = tick;
}

void VehiclePlayback::apply(viewer::VehicleLayer& vehicles, double renderTick,
                            const std::optional<Driver>& driver) {
    if (!std::isfinite(renderTick) || renderTick < 0) throw std::invalid_argument("Invalid vehicle playback time");
    if (driver && (!std::isfinite(driver->serverTick) || driver->serverTick < 0 || driver->vehicle >= cars_.size()))
        throw std::invalid_argument("Invalid driver vehicle prediction");
    if (vehicles.count() > cars_.size()) throw std::runtime_error("Client vehicle population exceeds server state");
    // Check every type before creating or moving any car in the layer.
    for (std::size_t id = 0; id < cars_.size(); ++id) {
        const auto type = cars_[id].samples.back().state.type;
        if (type >= vehicles.typeCount() || (id < vehicles.count() && vehicles.type(id) != type))
            throw std::runtime_error("Replicated vehicle type does not match session assets");
    }
    for (auto id = vehicles.count(); id < cars_.size(); ++id) {
        const auto& state = cars_[id].samples.back().state;
        vehicles.add(state.chassis.center, state.chassis.rotation, state.type);
    }
    for (std::size_t id = 0; id < cars_.size(); ++id) {
        auto& history = cars_[id];
        auto& samples = history.samples;
        while (samples.size() > 1 && double(samples[1].tick) <= renderTick) samples.pop_front();
        if (driver && driver->vehicle == id) {
            const auto& latest = samples.back();
            const auto& state = latest.state;
            const double age = std::clamp((driver->serverTick - double(latest.tick)) / 60,
                                          0.0, extrapolationSeconds);
            const auto translation = state.velocity * static_cast<float>(age);
            auto turn = glm::quat(1, 0, 0, 0);
            const float turnSpeed = glm::length(history.angularVelocity);
            // Omission in a newer complete snapshot confirms the car's pose
            // was unchanged. Do not keep turning after that confirmed stop.
            if (latest.tick == *latestTick_ && turnSpeed > 1e-6f)
                turn = glm::angleAxis(turnSpeed * static_cast<float>(age), history.angularVelocity / turnSpeed);
            auto pose = vehicles.pose(id);
            pose.chassis.center = state.chassis.center + translation;
            pose.chassis.rotation = glm::normalize(turn * state.chassis.rotation);
            for (std::size_t wheel = 0; wheel < pose.wheels.size(); ++wheel)
                pose.wheels[wheel] = glm::translate(glm::mat4(1), pose.chassis.center +
                    turn * (state.wheels[wheel].center - state.chassis.center)) *
                    glm::mat4_cast(glm::normalize(turn * state.wheels[wheel].rotation));
            vehicles.setPose(id, pose, state.velocity);
            // A subsequent exit/passenger transition must restore buffered
            // playback even if that playback tick has not advanced.
            history.appliedTick.reset();
            continue;
        }
        const double tick = std::clamp(renderTick, double(samples.front().tick), double(samples.back().tick));
        if (history.appliedTick && *history.appliedTick == tick) continue;
        const auto& a = samples.front();
        const auto& b = samples.size() > 1 ? samples[1] : a;
        const float t = a.tick == b.tick ? 1.f : static_cast<float>(
            (tick - double(a.tick)) / double(b.tick - a.tick));
        const auto chassis = blend(a.state.chassis, b.state.chassis, t);
        auto pose = vehicles.pose(id);
        pose.chassis.center = chassis.center;
        pose.chassis.rotation = chassis.rotation;
        for (std::size_t wheel = 0; wheel < pose.wheels.size(); ++wheel) {
            const auto state = blend(a.state.wheels[wheel], b.state.wheels[wheel], t);
            pose.wheels[wheel] = glm::translate(glm::mat4(1), state.center) * glm::mat4_cast(state.rotation);
        }
        vehicles.setPose(id, pose, glm::mix(a.state.velocity, b.state.velocity, t));
        history.appliedTick = tick;
    }
}
} // namespace multiplayer
