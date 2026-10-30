#pragma once

#include "protocol.hpp"
#include <optional>
#include <vector>

namespace viewer { struct AnimatedModel; }

namespace multiplayer {
// Bufferless prediction from two timestamped authoritative poses. Receive time
// anchors the local clock; sample spacing always comes from server ticks.
class RagdollPlayback {
public:
    void push(const CorpsePose& pose, double receivedSeconds, double transitSeconds = 0);
    const CorpsePose* sample(const viewer::AnimatedModel& asset, double now);
    std::uint64_t revision() const { return revision_; }
    // Retain the latest authoritative pose, but never blend across re-entry.
    void resetHistory();
private:
    struct Sample { CorpsePose pose; double seconds, transit; };
    std::vector<Sample> samples_;
    std::optional<CorpsePose> rendered_;
    double renderSeconds_ = -1;
    std::uint64_t revision_ = 0, sampledVersion_ = 0;
};
} // namespace multiplayer
