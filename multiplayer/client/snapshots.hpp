#pragma once
#include "scene.hpp"
#include <deque>
#include <array>

namespace multiplayer {
class Snapshots {
public:
    bool push(Snapshot snapshot, double receivedSeconds);
    const Snapshot& latest() const { return frames_.back(); }
    bool empty() const { return frames_.empty(); }
    void apply(Scene& scene, double now);
    const std::vector<PlayerState>& players() const { return players_; }
    double animationSeconds() const { return renderTick_ / 60; }
private:
    std::deque<Snapshot> frames_;
    std::vector<PlayerState> players_;
    double received_ = 0, renderTick_ = 0;
    std::array<std::vector<glm::mat4>, maxZombies> palettes_;
};
} // namespace multiplayer
