#pragma once
#include <glm/vec3.hpp>
#include <cstdint>

namespace viewer {
struct MapMark {
    std::uint64_t id = 0; // Session-wide stable identity; never reused.
    std::uint32_t player = 0;
    glm::vec3 position{0}; // Runtime X east/Y up/Z south metres.
};
}
