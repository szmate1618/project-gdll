#include "vehicle_replication.hpp"
#include "vehicle_layer.hpp"
#include <stdexcept>

namespace multiplayer {
void VehicleReplication::acknowledge(std::uint64_t tick) {
    if (!tick || !lastSnapshotTick_ || tick > *lastSnapshotTick_ ||
        (acknowledgedTick_ && tick < *acknowledgedTick_)) return;
    acknowledgedTick_ = tick;
    for (auto& entry : entries_)
        if (entry.pending && entry.firstSentTick && *entry.firstSentTick <= tick)
            entry.pending = false;
}

void VehicleReplication::append(const viewer::VehicleLayer& vehicles, std::uint64_t tick,
                                std::vector<VehicleState>& out) {
    if (lastSnapshotTick_ && tick < *lastSnapshotTick_)
        throw std::logic_error("Vehicle snapshot ticks moved backwards");
    if (vehicles.count() > maxVehicles)
        throw std::runtime_error("Vehicle replication budget exceeded");
    if (layerId_ != vehicles.instanceId()) {
        entries_.clear();
        layerId_ = vehicles.instanceId();
    }
    entries_.resize(vehicles.count());
    for (std::size_t id = 0; id < vehicles.count(); ++id) {
        auto& entry = entries_[id];
        const auto revision = vehicles.poseRevision(id);
        // Impulses and velocity-only explicit updates need not change the pose
        // revision. Sampling velocity is cheap and also publishes the final stop.
        const auto velocity = vehicles.velocity(id);
        if (!entry.initialized || entry.poseRevision != revision || entry.state.velocity != velocity) {
            const auto pose = vehicles.pose(id);
            entry.state.id = static_cast<std::uint32_t>(id);
            entry.state.type = static_cast<std::uint32_t>(vehicles.type(id));
            entry.state.chassis = {pose.chassis.center, pose.chassis.rotation};
            entry.state.velocity = velocity;
            for (std::size_t wheel = 0; wheel < pose.wheels.size(); ++wheel)
                entry.state.wheels[wheel] = {glm::vec3(pose.wheels[wheel][3]),
                    glm::normalize(glm::quat_cast(glm::mat3(pose.wheels[wheel])))};
            entry.poseRevision = revision;
            entry.firstSentTick.reset();
            entry.initialized = entry.pending = true;
        }
        if (!entry.pending) continue;
        // A previous snapshot with this same tick may contain an older state.
        // Tick zero also doubles as the command protocol's missing-ACK sentinel.
        if (!entry.firstSentTick && tick && (!lastSnapshotTick_ || tick > *lastSnapshotTick_))
            entry.firstSentTick = tick;
        out.push_back(entry.state);
    }
    lastSnapshotTick_ = tick;
}
} // namespace multiplayer
