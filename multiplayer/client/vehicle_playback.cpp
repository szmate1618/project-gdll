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
        auto& samples = cars_[state.id].samples;
        // An omitted car was unchanged at the preceding global snapshot. Add
        // that hold only when it moves again, so a long parked interval never
        // stretches its first movement across the entire silent history.
        if (!samples.empty() && latestTick_ && samples.back().tick < *latestTick_)
            samples.push_back({*latestTick_, samples.back().state});
        samples.push_back({tick, state});
        while (samples.size() > historySize) samples.pop_front();
    }
    latestTick_ = tick;
}

void VehiclePlayback::apply(viewer::VehicleLayer& vehicles, double renderTick) {
    if (!std::isfinite(renderTick) || renderTick < 0) throw std::invalid_argument("Invalid vehicle playback time");
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
