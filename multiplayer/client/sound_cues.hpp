#pragma once
#include "protocol.hpp"
#include "zombie_audio.hpp"
#include <map>

namespace multiplayer {
// Snapshot receipt adapter: deduplicates repeated events, suppresses startup
// history and stale cues, and never depends on render interpolation membership.
class RemoteSoundCues {
public:
    std::vector<viewer::AudioCue> receive(const Snapshot& snapshot, std::uint32_t self);
private:
    bool initialized_ = false;
    std::uint64_t joinedTick_ = 0;
    std::uint32_t lastExplosion_ = 0;
    std::map<std::uint64_t, std::uint64_t> seen_;
};
} // namespace multiplayer
