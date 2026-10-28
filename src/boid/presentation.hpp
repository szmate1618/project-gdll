#pragma once

#include "simulation.hpp"
#include <glm/vec3.hpp>
#include <cmath>

namespace viewer {
struct FlockBird {
    glm::vec3 position{0}, velocity{0};
    float phase = 0; // Render-only wing variation; never feeds simulation.
};
namespace boid {
inline Vec3 fromMeters(glm::vec3 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        std::abs(p.x)>coordinateLimit/unitsPerMeter || std::abs(p.y)>coordinateLimit/unitsPerMeter || std::abs(p.z)>coordinateLimit/unitsPerMeter)
        throw std::invalid_argument("Boid coordinate outside +/-100 km local frame");
    // Authority boundary only. Clients receive these integers verbatim.
    return {narrow(static_cast<std::int64_t>(std::round(double(p.x)*unitsPerMeter))),
            narrow(static_cast<std::int64_t>(std::round(double(p.y)*unitsPerMeter))),
            narrow(static_cast<std::int64_t>(std::round(double(p.z)*unitsPerMeter)))};
}
inline glm::vec3 toMeters(Vec3 p) { return glm::vec3(p.x,p.y,p.z)/float(unitsPerMeter); }
inline float wingPhase(std::uint32_t seed, std::size_t index) {
    return float(hash(seed^static_cast<std::uint32_t>(index))&65535u)*(6.28318530718f/65536.f);
}
inline void present(const Checkpoint& state, std::vector<FlockBird>& birds) {
    birds.resize(state.birds.size());
    for (std::size_t i=0;i<birds.size();++i)
        birds[i]={toMeters(state.birds[i].position),toMeters(state.birds[i].velocity),wingPhase(state.seed,i)};
}
} // namespace boid
} // namespace viewer
