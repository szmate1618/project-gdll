#include "zombie_crowd.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

glm::vec2 ZombieCrowd::chaseDirection(std::size_t id, glm::vec2 targetOffset) {
    const float distance = glm::length(targetOffset);
    if (!std::isfinite(distance) || distance < 1e-4f) return {0, 0};
    const auto forward = targetOffset / distance;
    if (id >= bodies_.size() || !bodies_[id].present) return forward;
    // Stop bending the final approach so steering cannot prevent arrival.
    const float fade = std::clamp((distance - .15f) / .85f, 0.f, 1.f);
    if (fade == 0) return forward;

    constexpr float range = 1.5f, spacing = 1.2f, laneWidth = bodyRadius * 2 + .05f;
    const auto& body = bodies_[id];
    const auto feet = body.feet;
    const Cell low{std::floor((double(feet.x) - range) / cellSize),
                   std::floor((double(feet.z) - range) / cellSize)};
    const Cell high{std::floor((double(feet.x) + range) / cellSize),
                    std::floor((double(feet.z) + range) / cellSize)};
    if (*body.sectorPopulation == 1 &&
        low.x >= body.sector.x * sectorCells && high.x < (body.sector.x + 1) * sectorCells &&
        low.z >= body.sector.z * sectorCells && high.z < (body.sector.z + 1) * sectorCells)
        return forward;

    const glm::vec2 right(forward.y, -forward.x);
    const float passingSide = id % 2 == 0 ? -1.f : 1.f;
    float leftPressure = 0, rightPressure = 0, passLeft = 0, passRight = 0;
    std::size_t checks = 0;
    const auto width = static_cast<std::size_t>(high.x - low.x + 1);
    const auto depth = static_cast<std::size_t>(high.z - low.z + 1);
    for (std::size_t x = 0; x < width; ++x) {
        for (std::size_t z = 0; z < depth; ++z) {
            const auto found = cells_.find({low.x + double(x), low.z + double(z)});
            if (found == cells_.end()) continue;
            for (const auto other : found->second) {
                if (other == id) continue;
                // An incomplete steering neighborhood falls back to direct
                // pursuit. The independent collision budget still enforces
                // clearance against every accepted movement segment.
                if (checks == neighborBudget) return forward;
                ++checks;
                ++stats_.steeringChecks;
                const auto otherFeet = bodies_[other].feet;
                if (otherFeet.y >= feet.y + bodyHeight || otherFeet.y + bodyHeight <= feet.y) continue;
                const glm::vec2 offset(otherFeet.x - feet.x, otherFeet.z - feet.z);
                const float length = glm::length(offset);
                if (length >= range) continue;
                const float across = glm::dot(offset, right);
                const float pressure = std::max(0.f, 1 - length / spacing) * across / std::max(length, .01f);
                // Maxima make the answer independent of bucket insertion
                // order and keep dense groups from increasing steering speed.
                leftPressure = std::max(leftPressure, pressure);
                rightPressure = std::max(rightPressure, -pressure);
                const float along = glm::dot(offset, forward);
                if (along > 0 && std::abs(across) < laneWidth) {
                    const float pressure = (1 - along / range) * (1 - std::abs(across) / laneWidth);
                    // Keep an already open flank; use the stable preference
                    // only when the blocker is nearly straight ahead.
                    const float side = std::abs(across) < .15f ? passingSide : (across > 0 ? -1.f : 1.f);
                    if (side < 0) passLeft = std::max(passLeft, pressure);
                    else passRight = std::max(passRight, pressure);
                }
            }
        }
    }
    // A straight queue has no lateral separation force. The persistent passing
    // side breaks that symmetry without random per-step changes or new state.
    const float sideways = fade * (.6f * (rightPressure - leftPressure) + 1.2f * (passRight - passLeft));
    return sideways == 0 ? forward : glm::normalize(forward + right * sideways);
}

} // namespace viewer
