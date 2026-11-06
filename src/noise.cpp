#include "noise.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {

float noiseLevelAt(const NoiseEvent& noise, glm::vec3 listener) {
    const auto offset = glm::dvec3(listener) - glm::dvec3(noise.position);
    const double distance = glm::length(offset);
    if (!std::isfinite(noise.decibels) || !std::isfinite(distance))
        return -std::numeric_limits<float>::infinity();
    return static_cast<float>(noise.decibels - 20.0 * std::log10(std::max(distance, 1.0)));
}
std::optional<NoiseEvent> zombieImpactNoise(glm::vec3 position, float speed) {
    if (!std::isfinite(speed) || speed < .8f) return {};
    return NoiseEvent{position, std::min(70.f, 55.f + 20.f * std::log10(speed / 2.f)),
        NoiseEvent::Kind::zombieImpact, NoiseAudience::playersOnly};
}

} // namespace viewer
