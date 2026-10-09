#pragma once

#include <cmath>

namespace viewer {

// Simulation-thread clock in seconds. Only the app advances it, once per
// simulation step; wall-clock waits and paused/minimized frames do not count.
class SimulationClock {
public:
    static SimulationClock& instance() {
        static SimulationClock clock;
        return clock;
    }

    [[nodiscard]] double seconds() const { return seconds_; }
    void advance(double seconds) {
        if (std::isfinite(seconds) && seconds > 0 && std::isfinite(seconds_ + seconds))
            seconds_ += seconds;
    }
    // Start a new simulation/session. Existing timed objects must be cleared.
    void reset() { seconds_ = 0; }

    SimulationClock(const SimulationClock&) = delete;
    SimulationClock& operator=(const SimulationClock&) = delete;

private:
    SimulationClock() = default;
    double seconds_ = 0;
};

} // namespace viewer
