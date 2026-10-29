#pragma once

#include "protocol.hpp"
#include <deque>
#include <optional>
#include <vector>

namespace viewer { class VehicleLayer; }

namespace multiplayer {
// Sparse authoritative poses in runtime meters (X east, Y up, Z south).
// Each car keeps its own bounded history on the shared world playback clock;
// omission confirms its last pose without removing it or advancing its history.
class VehiclePlayback {
public:
    void push(std::uint64_t tick, const std::vector<VehicleState>& updates);
    // Owns client vehicle poses; clients do not advance vehicle physics.
    // New cars are available immediately, including for newest-state occupancy.
    void apply(viewer::VehicleLayer& vehicles, double renderTick);

private:
    struct Sample { std::uint64_t tick; VehicleState state; };
    struct History {
        std::deque<Sample> samples;
        std::optional<double> appliedTick;
    };
    std::vector<History> cars_; // Dense stable IDs, matching VehicleLayer.
    std::optional<std::uint64_t> latestTick_;
};
} // namespace multiplayer
