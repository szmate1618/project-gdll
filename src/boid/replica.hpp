#pragma once

#include "presentation.hpp"
#include <deque>

namespace viewer::boid {
// Playback never performs scene queries or AI decisions. Tick watermarks make
// late reliable commands safe: do not predict beyond a completed frame.
class Replica {
public:
    // False requests a checkpoint after a missing command/checksum mismatch.
    bool receive(const Frame& frame);
    const std::vector<FlockBird>& sample(double seconds);
    const Simulation& simulation() const { return simulation_; }
    bool ready() const { return ready_; }
private:
    struct Pose { std::uint64_t tick=0; std::vector<FlockBird> birds; };
    Simulation simulation_;
    std::deque<Pose> poses_;
    std::vector<FlockBird> displayed_;
    bool ready_=false;
};
} // namespace viewer::boid
