#pragma once
#include "scene.hpp"
#include <map>
#include <list>
#include <optional>

namespace multiplayer {
// Per-peer interest and bulk transfer state. Essential snapshots never carry
// palettes. Reliable transport retries each bounded in-flight pose; application
// acknowledgements prevent a slow peer from accumulating old pose versions.
class Replication {
public:
    Replication() = default;
    Replication(const Replication&) = delete;
    Replication& operator=(const Replication&) = delete;
    Replication(Replication&&) noexcept = default;
    Replication& operator=(Replication&&) noexcept = default;
    static constexpr std::size_t maxPoseTransfers = 4;
    Snapshot snapshot(Scene& scene, glm::vec3 feet, Snapshot state,
                      const std::optional<ReplicationView>& view = {});
    std::optional<ZombieSetups> nextSetups(const Scene& scene) const;
    void setupsSent(const ZombieSetups& setups);
    void acknowledgeSetups(const ZombieSetupAck& ack);
    std::optional<CorpsePose> nextPose(const Scene& scene) const;
    void sent(const CorpsePose& pose);
    void acknowledge(const PoseAck& ack);
    std::size_t interestedCount() const { return interestedIds_.size(); }
private:
    struct SetupState {
        ZombieSetup current;
        std::uint64_t acknowledged = 0, inFlight = 0, lastMotionTick = 0;
        std::uint8_t lastMode = 0, lastPeriod = 0;
        bool sentMotion = false, queued = false;
        std::list<std::size_t>::iterator pending;
    };
    SetupState* findSetup(std::size_t id);
    // World IDs map to dense setup records, storing record index + 1 so zero
    // denotes an unseen actor. Cached records retain metadata for re-entry.
    std::vector<std::uint32_t> setupSlots_;
    std::vector<SetupState> setups_;
    std::list<std::size_t> pendingSetups_;
    std::size_t setupTransfers_ = 0;
    std::vector<std::size_t> selected_, interestedIds_, interestedCorpses_;
    // Sparse clearing visits the preceding interest list, never all world IDs.
    std::vector<std::uint8_t> interested_;
    std::map<std::size_t, std::uint64_t> acknowledged_, inFlight_;
    std::size_t nextPoseId_ = 0;
};
} // namespace multiplayer
