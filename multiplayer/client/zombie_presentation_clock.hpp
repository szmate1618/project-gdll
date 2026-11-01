#pragma once

#include <cstdint>

namespace multiplayer {
// One clock per session for living zombie roots and animation. Accepted newer
// snapshots observe server ticks; local timestamps are steady-clock seconds.
class ZombiePresentationClock {
public:
    void observe(std::uint64_t serverTick, double receivedSeconds, double transitSeconds);
    double tick(double now);

private:
    void advance(double now);
    double localSeconds_ = 0, serverSeconds_ = 0, targetOffset_ = 0;
    bool initialized_ = false;
};
} // namespace multiplayer
