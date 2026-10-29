#pragma once

#include "protocol.hpp"
#include <optional>

namespace viewer { class VehicleLayer; }

namespace multiplayer {
// Per-peer persistent vehicle updates. Every pending current state accompanies
// later snapshots until the client acknowledges a snapshot carrying it. Lost
// initial or final poses therefore retry without retaining intermediate poses.
class VehicleReplication {
public:
    // Supply Commands::snapshotTick, the last complete snapshot received by the
    // client. Zero means no receipt; it cannot confirm the tick-zero baseline.
    // Backward and future receipts cannot acknowledge subsequently sent states.
    void acknowledge(std::uint64_t snapshotTick);
    // Ticks must not decrease. A replacement published at an already used tick
    // needs a later positive snapshot before its receipt can confirm the change.
    // IDs are stable within one VehicleLayer lifetime; a new layer resets state.
    void append(const viewer::VehicleLayer& vehicles, std::uint64_t tick,
                std::vector<VehicleState>& out);
private:
    struct Entry {
        VehicleState state;
        std::uint64_t poseRevision = 0;
        std::optional<std::uint64_t> firstSentTick;
        bool initialized = false, pending = false;
    };
    std::vector<Entry> entries_;
    std::uint64_t layerId_ = 0;
    std::optional<std::uint64_t> lastSnapshotTick_, acknowledgedTick_;
};
} // namespace multiplayer
