#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <glm/vec2.hpp>

namespace viewer::steering {

// All horizontal vectors are runtime X/Z meters or meters per second.
enum class Mode { collision_only, gradient, gradient_side, perpendicular_push, detour };
const char* modeName(Mode mode) noexcept;
struct Obstacle { std::uint32_t id; glm::vec2 center; float radius; };
struct State {
    glm::vec2 velocity{}; // Caller writes actual accepted movement / dt after each step.
    std::uint32_t obstacle=UINT32_MAX;
    int side=0;
};
struct Config {
    float radius=.30f, skin=.002f, influenceRange=3.f;
    float gradientWeight=16.f, horizon=1.5f, tangentWeight=.8f;
};
struct Stats {
    std::uint64_t obstacleTests=0, velocitySamples=0, sideChanges=0;
};

// Geometry-only helpers permit independent finite-difference verification.
// A compact C2 taper is included in both the potential and its exact derivative.
float potential(glm::vec2 position,const Obstacle* obstacles,std::size_t count,const Config& config);
glm::vec2 gradient(glm::vec2 position,const Obstacle* obstacles,std::size_t count,const Config& config);

// One reusable controller per simulation thread, shared sequentially by agents.
// Construction allocates Detour's fixed workspace; steer performs no allocation.
// Input count must fit maxObstacles; overflow throws rather than dropping trunks.
// This adjusts velocity only. Final body collision and terrain remain external.
class Controller {
public:
    explicit Controller(Config config={},std::size_t maxObstacles=256);
    ~Controller();
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    glm::vec2 steer(Mode mode,glm::vec2 position,glm::vec2 desiredVelocity,float dt,
                    const Obstacle* obstacles,std::size_t count,State& state,Stats& stats);
    std::size_t workspaceBytes() const noexcept;
    const Config& config() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer::steering
