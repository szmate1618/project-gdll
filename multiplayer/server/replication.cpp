#include "replication.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
Snapshot Replication::snapshot(Scene& scene, glm::vec3 feet, Snapshot s) {
    for (const auto door : scene.doors->activeDoors()) {
        const auto offset = scene.doors->panel(door).center - feet;
        if (glm::dot(offset, offset) < 240.0f * 240 && s.doors.size() < maxDoors)
            s.doors.push_back({static_cast<std::uint32_t>(door), scene.doors->angle(door)});
    }
    for (std::size_t i = 0; i < scene.boxes->count(); ++i) {
        const auto p = scene.boxes->pose(i);
        s.boxes.push_back({static_cast<std::uint32_t>(i), p.center, p.halfExtents, p.rotation});
    }
    std::vector<std::size_t> candidates;
    scene.zombies.queryLivingNear(feet, 240, candidates);
    candidates.insert(candidates.end(), scene.zombies.ragdollIds().begin(), scene.zombies.ragdollIds().end());
    const auto distance = [&](std::size_t z) {
        const auto& instance = scene.zombies.instances[z];
        const glm::vec3 position(instance.ragdoll ? instance.transform[3] : instance.restRoot[3]);
        const auto d = position - feet; return glm::dot(d, d);
    };
    std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
        const auto da = distance(a), db = distance(b); return da == db ? a < b : da < db;
    });
    std::set<std::size_t> interested;
    for (const auto z : candidates) {
        const auto& instance = scene.zombies.instances[z];
        const float radius = interested_.count(z) ? 240.0f : 220.0f;
        if (instance.removed || distance(z) > radius * radius || s.zombies.size() == maxVisibleZombies) continue;
        s.zombies.push_back({static_cast<std::uint32_t>(z), static_cast<std::uint32_t>(instance.asset),
            instance.transform, instance.restRoot, instance.phase, static_cast<float>(instance.walkSeconds),
            instance.walking, instance.running, instance.ragdoll});
        if (const auto* wound = scene.zombies.wounds().find(z)) s.zombies.back().woundMask = wound->vertexMask;
        interested.insert(z);
    }
    interested_ = std::move(interested);
    s.woundContents = scene.zombies.woundContents();
    return s;
}
std::optional<CorpsePose> Replication::nextPose(const Scene& scene) const {
    if (inFlight_.size() >= maxPoseTransfers) return std::nullopt;
    // Round-robin prevents continuously moving nearby corpses starving late joins.
    for (std::size_t offset = 0; offset < maxZombies; ++offset) {
        const auto id = (nextPoseId_ + offset) % maxZombies;
        if (!interested_.count(id) || inFlight_.count(id)) continue;
        const auto& instance = scene.zombies.instances.at(id);
        if (!instance.ragdoll || instance.removed) continue;
        const auto ack = acknowledged_.find(id);
        if (ack != acknowledged_.end() && ack->second >= instance.ragdollPoseVersion) continue;
        return CorpsePose{static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(instance.asset),
            instance.ragdollPoseVersion, instance.transform, instance.restRoot, instance.ragdollBones,
            static_cast<std::int16_t>(instance.severedJoint)};
    }
    return std::nullopt;
}
void Replication::sent(const CorpsePose& pose) {
    if (inFlight_.size() >= maxPoseTransfers || inFlight_.count(pose.id))
        throw std::logic_error("Corpse transfer already active or full");
    inFlight_.emplace(pose.id, pose.version);
    nextPoseId_ = (pose.id + 1) % maxZombies;
}
void Replication::acknowledge(const PoseAck& ack) {
    const auto sent = inFlight_.find(ack.id);
    const auto previous = acknowledged_.find(ack.id);
    if (previous != acknowledged_.end() && ack.version <= previous->second) return;
    if (sent == inFlight_.end() || ack.version != sent->second)
        throw std::runtime_error("Client acknowledged an unsent corpse pose");
    acknowledged_[ack.id] = ack.version;
    inFlight_.erase(sent);
}
} // namespace multiplayer
