#pragma once

#include "protocol.hpp"
#include <glm/gtc/quaternion.hpp>
#include <optional>

namespace multiplayer {

struct PredictedZombieMotion {
    glm::mat4 root{1};
    double walkSeconds = 0;
    double idleSeconds = 0;
};

// Presentation only: authoritative roots are runtime X east/Y up/Z south
// meters. Server ticks estimate motion; local seconds time brief corrections.
// Asset normalization and the authoritative simulation stay outside this class.
class ZombiePrediction {
public:
    void push(std::uint64_t tick, const ZombieState& state);
    PredictedZombieMotion sample(double estimatedServerTick, double now);
    const ZombieState& latest() const { return source_->state; }
    std::uint64_t latestTick() const { return source_->tick; }
    bool empty() const { return !source_; }
    void reset();

private:
    struct Source {
        ZombieState state;
        std::uint64_t tick = 0;
        glm::vec3 velocity{0}, angularVelocity{0};
        double walkRate = 0;
        bool measured = false;
    };
    static PredictedZombieMotion predict(const Source& source, double serverTick);
    double correctionWeight(double now) const;

    std::optional<Source> source_, previousPrediction_;
    glm::vec3 positionCorrection_{0};
    glm::quat rotationCorrection_{1, 0, 0, 0};
    double walkCorrection_ = 0, correctionStarted_ = -1;
    bool sampled_ = false;
};

} // namespace multiplayer
