#include "zombie_prediction.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace multiplayer {
namespace {
constexpr double ticksPerSecond = 60;
constexpr double correctionSeconds = .05;
constexpr float maximumSpeed = 12; // m/s; larger jumps are authoritative discontinuities.
constexpr float maximumTurnRate = 3.141592654f; // rad/s, matching locomotion's 180 degrees/s.
constexpr float maximumCorrection = .5f; // meters; large corrections snap immediately.
constexpr float maximumCorrectionAngle = .785398163f; // radians, 45 degrees.

double horizon(const ZombieState& state) {
    // Near actors cover a missed 20 Hz update. Slow actors cover one advertised
    // interval, never more than a second of unconfirmed motion.
    return std::clamp(double(state.updatePeriodTicks) / ticksPerSecond, .1, 1.0);
}

glm::quat rotation(const glm::mat4& root) {
    return glm::normalize(glm::quat_cast(glm::mat3(root)));
}

glm::quat relativeRotation(glm::quat before, glm::quat after) {
    auto turn = glm::normalize(after * glm::conjugate(before));
    return turn.w < 0 ? -turn : turn;
}

float angle(glm::quat turn) {
    return 2 * std::acos(std::clamp(turn.w, 0.f, 1.f));
}
} // namespace

void ZombiePrediction::push(std::uint64_t tick, const ZombieState& state) {
    if (source_ && tick <= source_->tick) return;
    Source next;
    next.state = state;
    next.tick = tick;
    next.walkRate = state.walking && !state.dead ? 1 : 0;
    if (source_) {
        const auto& old = *source_;
        const double interval = double(tick - old.tick) / ticksPerSecond;
        const auto displacement = glm::vec3(state.root[3] - old.state.root[3]);
        const bool compatible = !old.state.dead && !state.dead &&
            state.walking == old.state.walking && state.running == old.state.running &&
            !state.attackClip && !old.state.attackClip &&
            interval <= std::max(.2, 2 * std::max(horizon(old.state), horizon(state))) &&
            glm::length(displacement) <= maximumSpeed * interval;
        if (compatible) {
            next.measured = true;
            if (state.walking) next.velocity = displacement / float(interval);
            const auto turn = relativeRotation(rotation(old.state.root), rotation(state.root));
            const float radians = angle(turn);
            const auto axis = glm::vec3(turn.x, turn.y, turn.z);
            if (glm::length(axis) > 1e-6f)
                next.angularVelocity = glm::normalize(axis) * std::min(maximumTurnRate, radians / float(interval));
            // Locomotion time is actual travel / requested speed, not wall
            // time; contact and arrival can reduce its measured rate to zero.
            next.walkRate = state.walking ? std::clamp(
                (double(state.walkSeconds) - old.state.walkSeconds) / interval, 0.0, 1.0) : 0;
            if (sampled_ && old.measured && !previousPrediction_) previousPrediction_ = old;
        } else {
            next.walkRate = 0;
            previousPrediction_.reset();
            positionCorrection_ = glm::vec3(0);
            rotationCorrection_ = glm::quat(1, 0, 0, 0);
            walkCorrection_ = 0;
            correctionStarted_ = -1;
        }
    }
    source_ = std::move(next);
}

PredictedZombieMotion ZombiePrediction::predict(const Source& source, double serverTick) {
    const auto& state = source.state;
    const double age = state.dead ? 0 : std::clamp(
        (serverTick - double(source.tick)) / ticksPerSecond, 0.0, horizon(state));
    PredictedZombieMotion result{state.root, double(state.walkSeconds) + source.walkRate * age,
        double(source.tick) / ticksPerSecond + age};
    const float turnRate = glm::length(source.angularVelocity);
    if (turnRate > 1e-6f) result.root = glm::mat4_cast(
        glm::angleAxis(turnRate * float(age), source.angularVelocity / turnRate) * rotation(state.root));
    result.root[3] = glm::vec4(glm::vec3(state.root[3]) + source.velocity * float(age), 1);
    return result;
}

double ZombiePrediction::correctionWeight(double now) const {
    return correctionStarted_ < 0 ? 0 : std::clamp(1 - (now - correctionStarted_) / correctionSeconds, 0.0, 1.0);
}

PredictedZombieMotion ZombiePrediction::sample(double estimatedServerTick, double now) {
    if (!source_) throw std::logic_error("Cannot sample zombie motion before its first update");
    if (!std::isfinite(estimatedServerTick) || !std::isfinite(now))
        throw std::invalid_argument("Invalid zombie prediction time");
    auto result = predict(*source_, estimatedServerTick);
    if (previousPrediction_) {
        // Compare trajectories at the same time, rather than blending from the
        // previous rendered frame: regular constant-speed updates add no lag.
        auto previous = predict(*previousPrediction_, estimatedServerTick);
        const float remaining = float(correctionWeight(now));
        previous.root[3] += glm::vec4(positionCorrection_ * remaining, 0);
        const auto oldRotation = glm::slerp(glm::quat(1, 0, 0, 0), rotationCorrection_, remaining) * rotation(previous.root);
        const auto turn = relativeRotation(rotation(result.root), oldRotation);
        const auto displacement = glm::vec3(previous.root[3] - result.root[3]);
        const double walkError = previous.walkSeconds + walkCorrection_ * remaining - result.walkSeconds;
        if (glm::length(displacement) <= maximumCorrection && angle(turn) <= maximumCorrectionAngle &&
            std::abs(walkError) <= .1) {
            positionCorrection_ = displacement;
            rotationCorrection_ = turn;
            walkCorrection_ = walkError;
            correctionStarted_ = now;
        } else {
            positionCorrection_ = glm::vec3(0);
            rotationCorrection_ = glm::quat(1, 0, 0, 0);
            walkCorrection_ = 0;
            correctionStarted_ = -1;
        }
        previousPrediction_.reset();
    }
    const float remaining = float(correctionWeight(now));
    if (remaining > 0) {
        const auto position = result.root[3] + glm::vec4(positionCorrection_ * remaining, 0);
        result.root = glm::mat4_cast(glm::slerp(glm::quat(1, 0, 0, 0), rotationCorrection_, remaining) * rotation(result.root));
        result.root[3] = position;
        result.walkSeconds += walkCorrection_ * remaining;
    }
    sampled_ = true;
    return result;
}

void ZombiePrediction::reset() { *this = ZombiePrediction{}; }

} // namespace multiplayer
