#pragma once
#include "scene.hpp"
#include <deque>
#include <array>

namespace multiplayer {
class Snapshots {
public:
    bool push(Snapshot snapshot, double receivedSeconds);
    void pushPose(const Scene& scene, const CorpsePose& pose);
    const Snapshot& latest() const { return frames_.back(); }
    bool empty() const { return frames_.empty(); }
    void apply(Scene& scene, double now);
    const std::vector<PlayerState>& players() const { return players_; }
    double animationSeconds() const { return renderTick_ / 60; }
private:
    std::deque<Snapshot> frames_;
    std::vector<PlayerState> players_;
    double received_ = 0, renderTick_ = 0;
    std::array<std::optional<CorpsePose>, maxZombies> poses_;
    std::array<std::uint64_t, maxZombies> appliedPoseVersions_{};
};
} // namespace multiplayer
