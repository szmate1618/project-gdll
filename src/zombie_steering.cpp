#include "zombie_steering.hpp"
#include "DetourObstacleAvoidance.h"

#include <cmath>
#include <climits>
#include <new>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace viewer::steering {
namespace {
bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}
}

struct Controller::Impl {
    Config config;
    std::size_t maxObstacles;
    dtObstacleAvoidanceQuery query;
    dtObstacleAvoidanceParams params{};

    Impl(Config settings, std::size_t maximum) : config(settings), maxObstacles(maximum) {
        if (!std::isfinite(config.radius) || config.radius <= 0 ||
            !std::isfinite(config.skin) || config.skin < 0 ||
            !std::isfinite(config.radius + config.skin) ||
            !std::isfinite(config.horizon) || config.horizon <= 0 ||
            !maximum || maximum > INT_MAX)
            throw std::invalid_argument("invalid Detour steering configuration");
        // The stationary-tree adapter supplies no segments. One unused slot
        // avoids relying on malloc(0) behavior in unmodified upstream init().
        if (!query.init(static_cast<int>(maximum), 1)) throw std::bad_alloc();
        // Fixed adaptive quality/penalties, matching the evaluated adapter.
        params.velBias = .4f;
        params.weightDesVel = 2.f;
        params.weightCurVel = .75f;
        params.weightSide = .75f;
        params.weightToi = 2.5f;
        params.horizTime = config.horizon;
        params.gridSize = 33;
        params.adaptiveDivs = 7;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 3;
    }
};

Controller::Controller(Config config, std::size_t maxObstacles)
    : impl_(std::make_unique<Impl>(config, maxObstacles)) {}
Controller::~Controller() = default;
const Config& Controller::config() const noexcept { return impl_->config; }
std::size_t Controller::capacity() const noexcept { return impl_->maxObstacles; }
std::size_t Controller::workspaceBytes() const noexcept {
    return sizeof(Impl) + impl_->maxObstacles * sizeof(dtObstacleCircle) + sizeof(dtObstacleSegment);
}

glm::vec2 Controller::steer(glm::vec2 position, glm::vec2 currentVelocity,
                          glm::vec2 desiredVelocity, const Obstacle* obstacles,
                          std::size_t count, Stats* stats) {
    if (count > impl_->maxObstacles)
        throw std::length_error("Detour steering obstacle capacity exceeded");
    const float speedSquared = glm::dot(desiredVelocity, desiredVelocity);
    if (!finite(position) || !finite(currentVelocity) || !finite(desiredVelocity) ||
        !std::isfinite(speedSquared) || !std::isfinite(glm::dot(currentVelocity, currentVelocity)) ||
        (count && !obstacles))
        throw std::invalid_argument("invalid Detour steering step");
    for (std::size_t i = 0; i < count; ++i)
        if (!finite(obstacles[i].center) || !std::isfinite(obstacles[i].radius) || obstacles[i].radius <= 0)
            throw std::invalid_argument("invalid Detour steering obstacle");
    if (!count || speedSquared <= 1e-16f) return desiredVelocity;

    impl_->query.reset();
    constexpr float stationary[3] = {0, 0, 0};
    for (std::size_t i = 0; i < count; ++i) {
        const float center[3] = {obstacles[i].center.x, 0, obstacles[i].center.y};
        impl_->query.addCircle(center, obstacles[i].radius, stationary, stationary);
    }
    const float position3[3] = {position.x, 0, position.y};
    const float current3[3] = {currentVelocity.x, 0, currentVelocity.y};
    const float desired3[3] = {desiredVelocity.x, 0, desiredVelocity.y};
    float output[3]{};
    const float speed = std::sqrt(speedSquared);
    const int samples = impl_->query.sampleVelocityAdaptive(position3, impl_->config.radius + impl_->config.skin,
        speed, current3, desired3, output, &impl_->params);
    if (stats) {
        stats->obstacleTests += count;
        stats->velocitySamples += static_cast<std::uint64_t>(samples);
    }
    glm::vec2 velocity(output[0], output[2]);
    const float outputSquared = glm::dot(velocity, velocity);
    // A bad numerical sample cannot enter the physical movement pipeline.
    if (!finite(velocity) || !std::isfinite(outputSquared)) return glm::vec2(0);
    if (outputSquared > speedSquared) velocity *= speed / std::sqrt(outputSquared);
    return velocity;
}

} // namespace viewer::steering
