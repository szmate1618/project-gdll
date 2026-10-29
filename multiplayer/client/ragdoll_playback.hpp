#pragma once

#include "protocol.hpp"
#include <optional>
#include <vector>

namespace viewer { struct AnimatedModel; }

namespace multiplayer {
// Receive-time playback of complete poses; the wire format has no pose timestamp.
// History is bounded per corpse, independent of ordinary zombie presence samples.
class RagdollPlayback {
public:
    void push(const CorpsePose& pose, double receivedSeconds);
    const CorpsePose* sample(const viewer::AnimatedModel& asset, double now);
    std::uint64_t revision() const { return revision_; }
    // Retain the latest authoritative pose, but never blend across re-entry.
    void resetHistory();
private:
    struct Sample { CorpsePose pose; double seconds; };
    std::vector<Sample> samples_;
    std::optional<CorpsePose> rendered_;
    double delay_ = .1, renderSeconds_ = -1;
    std::uint64_t revision_ = 0, sampledVersion_ = 0;
};
} // namespace multiplayer
