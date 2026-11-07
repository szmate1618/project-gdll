#pragma once
#include "blood_spurt.hpp"

namespace viewer { class ZombieLayer; class CollisionWorld; }
namespace multiplayer {
// Snapshot history repeats starts to survive loss. Play each ID once, when its
// server timestamp reaches presentation, then age effects on the local clock.
class BloodPlayback {
public:
    void apply(const std::vector<viewer::BloodSpurtStart>& starts, double snapshotSeconds,
               double presentationSeconds, double now, viewer::ZombieLayer& zombies,
               const viewer::CollisionWorld& world);
private:
    std::array<std::uint32_t,viewer::BloodSpurts::capacity> seen_{};
};
} // namespace multiplayer
