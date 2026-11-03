#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>

namespace viewer {

// A panel's original aperture, in world X east / Y up / Z south meters.
// source identifies the owning collision world; door is its stable panel ID.
// These are guidance hints: current scene collision still validates passage.
struct DoorwayAperture {
    std::uint64_t source = 0;
    std::size_t door = 0;
    glm::vec3 center{0}, right{1, 0, 0}, outward{0, 0, 1};
    float width = 0, bottom = 0, top = 0;
    bool open = false; // At least 80 degrees; collision remains authoritative.
};

} // namespace viewer
