#pragma once

namespace viewer {

// Game hours in [0, 24); a full day takes dayLengthSeconds of simulation time.
// Zero freezes the clock. This clock is independent of animation capture time.
struct DayNightCycle {
    double startHour = 12.0;
    double dayLengthSeconds = 1200.0;
    double hourAt(double elapsedSeconds) const;
    // Absolute simulation seconds. A clock frozen away from midnight returns
    // infinity; spawning exactly at midnight can activate immediately.
    double nextMidnightAtOrAfter(double elapsedSeconds) const;
};

} // namespace viewer
