#include "player_noise.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

std::vector<NoiseEvent> PlayerNoiseEmitter::update(float seconds, const PlayerNoiseMotion& before,
                                                  const PlayerNoiseMotion& after, bool running) {
    std::vector<NoiseEvent> noises;
    const auto delta = after.feet - before.feet;
    if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(glm::length(delta))) return noises;
    if (!before.grounded && after.grounded) {
        noises.push_back({after.feet, landingDecibels, NoiseEvent::Kind::landing});
        reset();
        return noises;
    }
    const glm::vec2 horizontal(delta.x, delta.z);
    if (!before.grounded || !after.grounded || glm::dot(horizontal, horizontal) <= 1e-12f) {
        reset();
        return noises;
    }

    const double cadence = running ? 3.0 : 1.5;
    const double initialPhase = footstepPhase_;
    footstepPhase_ += seconds * cadence;
    for (std::size_t step = 1; footstepPhase_ >= 1.0; ++step) {
        const auto fraction = static_cast<float>(std::clamp((step - initialPhase) / (seconds * cadence), 0.0, 1.0));
        noises.push_back({glm::mix(before.feet, after.feet, fraction),
                          running ? runningFootstepDecibels : walkingFootstepDecibels,
                          NoiseEvent::Kind::footstep});
        footstepPhase_ -= 1.0;
    }
    return noises;
}

} // namespace viewer
