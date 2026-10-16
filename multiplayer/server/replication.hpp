#pragma once
#include "scene.hpp"
#include <map>
#include <optional>
#include <set>

namespace multiplayer {
// Per-peer interest and bulk transfer state. Essential snapshots never carry
// palettes. Reliable transport retries each bounded in-flight pose; application
// acknowledgements prevent a slow peer from accumulating old pose versions.
class Replication {
public:
    static constexpr std::size_t maxPoseTransfers = 4;
    Snapshot snapshot(Scene& scene, glm::vec3 feet, Snapshot state);
    std::optional<CorpsePose> nextPose(const Scene& scene) const;
    void sent(const CorpsePose& pose);
    void acknowledge(const PoseAck& ack);
private:
    std::set<std::size_t> interested_;
    std::map<std::size_t, std::uint64_t> acknowledged_, inFlight_;
    std::size_t nextPoseId_ = 0;
};
} // namespace multiplayer
