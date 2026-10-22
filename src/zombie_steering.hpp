#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <glm/vec2.hpp>

namespace viewer::steering {

// Horizontal vectors use world X/Z meters or meters per second. Obstacles are
// stationary trunk disks; their radii exclude the moving body's own radius.
struct Obstacle { glm::vec2 center; float radius; };
struct Config {
    float radius = .30f;
    float skin = .002f;
    float horizon = 1.5f; // Seconds of predicted motion used by Detour.
};
struct Stats {
    std::uint64_t obstacleTests = 0;
    std::uint64_t velocitySamples = 0;
};

// One fixed-capacity, reusable Detour query per simulation thread. Construction
// allocates its workspace; steer does not allocate and does not retain input.
// Feed indexed nearby trunks out to at least speed*horizon + radius + skin
// from each trunk's surface. A truncated neighborhood must stop movement at the
// caller rather than silently omitting obstacles.
//
// currentVelocity is the previous actual accepted displacement divided by dt.
// The returned preferred velocity still needs terrain/scene/crowd collision
// handling. Local sampling neither guarantees collision freedom nor replans.
class Controller {
public:
    explicit Controller(Config config = {}, std::size_t maxObstacles = 256);
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // Caps output speed to |desiredVelocity|; zero desired velocity stops.
    // Invalid geometry/configuration and capacity overflow throw explicitly.
    glm::vec2 steer(glm::vec2 position, glm::vec2 currentVelocity,
                    glm::vec2 desiredVelocity, const Obstacle* obstacles,
                    std::size_t count, Stats* stats = nullptr);
    const Config& config() const noexcept;
    std::size_t capacity() const noexcept;
    std::size_t workspaceBytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer::steering
