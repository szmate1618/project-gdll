#pragma once

#include "protocol.hpp"
#include <optional>
#include <vector>

namespace viewer { class VehicleLayer; }

namespace multiplayer {
// Sparse authoritative poses in runtime meters (X east, Y up, Z south).
// Each car extrapolates its latest linear and measured angular velocity at the
// estimated current server time. Omission retains its confirmed state.
class VehiclePlayback {
public:
    void push(std::uint64_t tick, const std::vector<VehicleState>& updates);
    // Owns client vehicle poses; clients do not advance vehicle physics.
    // New cars are available immediately, including for newest-state occupancy.
    void apply(viewer::VehicleLayer& vehicles, double serverTick);

private:
    struct Replica {
        VehicleState latest;
        std::uint64_t tick = 0;
        glm::vec3 angularVelocity{0}; // World-space radians/second, measured on accepted updates.
        std::optional<double> appliedAge;
        bool appliedTurning = false;
    };
    std::vector<Replica> cars_; // Dense stable IDs, matching VehicleLayer.
    std::optional<std::uint64_t> latestTick_;
};
} // namespace multiplayer
