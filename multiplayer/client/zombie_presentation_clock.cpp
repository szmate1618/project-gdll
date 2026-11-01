#include "zombie_presentation_clock.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
namespace {
constexpr double ticksPerSecond = 60;
constexpr double correctionSeconds = .25;
constexpr double maximumSlewRate = .1; // Seconds of clock correction per local second.
}

void ZombiePresentationClock::observe(std::uint64_t serverTick, double receivedSeconds, double transitSeconds) {
    if (!std::isfinite(receivedSeconds) || !std::isfinite(transitSeconds) || transitSeconds < 0)
        throw std::invalid_argument("Invalid zombie clock observation");
    const double serverSeconds = double(serverTick) / ticksPerSecond + std::min(transitSeconds, .5);
    if (!initialized_) {
        localSeconds_ = receivedSeconds;
        serverSeconds_ = serverSeconds;
        initialized_ = true;
    } else {
        // Finish elapsed time against the old observation. A newly arrived
        // packet cannot change time already presented, even within this frame.
        advance(receivedSeconds);
    }
    targetOffset_ = serverSeconds - receivedSeconds;
}

void ZombiePresentationClock::advance(double now) {
    if (!std::isfinite(now)) throw std::invalid_argument("Invalid zombie clock time");
    const double elapsed = std::max(0.0, now - localSeconds_);
    const double error = (localSeconds_ + targetOffset_) - serverSeconds_;
    const double correction = std::clamp(error * -std::expm1(-elapsed / correctionSeconds),
        -maximumSlewRate * elapsed, maximumSlewRate * elapsed);
    // Clock speed stays between 90% and 110% of real time. Unlike clamping a
    // packet-derived tick, this smooths both forward and backward corrections.
    serverSeconds_ += elapsed + correction;
    localSeconds_ += elapsed;
}

double ZombiePresentationClock::tick(double now) {
    if (!initialized_) throw std::logic_error("Cannot sample zombie clock before its first snapshot");
    advance(now);
    return serverSeconds_ * ticksPerSecond;
}
} // namespace multiplayer
