#pragma once
#include <cstdint>
#include <cstddef>
#include <glm/glm.hpp>

namespace viewer {
// The authority publishes the bottle impact only. Surface rays and fire patches
// belong to each client's presentation, in runtime world metres (Y up).
struct MolotovImpact {
    std::uint32_t id = 0;
    glm::vec3 position{0}, direction{0,-1,0};
    float age = 0;
};
inline constexpr float molotovFireSeconds = 10, molotovRayReach = 2;
inline constexpr std::size_t molotovRayCount = 10;
} // namespace viewer
