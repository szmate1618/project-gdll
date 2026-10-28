#pragma once

#include "boid/bird_flock.hpp"
#include "protocol.hpp"
#include <deque>
#include <memory>
#include <optional>

namespace multiplayer {
struct Scene;
// Only this authority queries scene geometry and chooses behavior. Clients
// receive integer controls, never rerun route selection or player perception.
class ServerBirds {
public:
    explicit ServerBirds(const Scene& scene);
    bool active() const { return birds_ && !birds_->birds().empty(); }
    std::vector<unsigned> step(const std::vector<viewer::BirdFlockPlayer>& players);
    std::optional<BirdFrame> frame(std::optional<std::uint64_t> since) const;
private:
    std::unique_ptr<viewer::BirdFlocks> birds_;
    std::deque<viewer::boid::Command> history_;
    mutable std::uint64_t checksumTick_=UINT64_MAX, checksum_=0;
};
} // namespace multiplayer
