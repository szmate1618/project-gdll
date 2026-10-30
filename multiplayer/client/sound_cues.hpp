#pragma once
#include "protocol.hpp"
#include "zombie_audio.hpp"
#include <limits>
#include <map>

namespace multiplayer {
// Deduplicates authoritative history and suppresses startup, self and stale
// events. Shot sound and optional visual events share one playback-time receipt.
class RemoteSoundCues {
public:
    std::vector<viewer::AudioCue> receive(const Snapshot& snapshot, std::uint32_t self,
        std::vector<SoundEvent>* shots = nullptr,
        double playbackSeconds = std::numeric_limits<double>::infinity());
private:
    bool initialized_ = false;
    std::uint64_t joinedTick_ = 0, latestTick_ = 0;
    std::uint32_t lastExplosion_ = 0;
    std::map<std::uint64_t, std::uint64_t> seen_;
};
} // namespace multiplayer
