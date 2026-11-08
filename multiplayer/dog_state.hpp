#pragma once
#include "dog_layer.hpp"
#include <cstdint>
namespace multiplayer {
constexpr std::size_t maxDogUpdatesPerSnapshot = 256;
// Self-contained samples use a dog-only stable-ID namespace. Missing IDs are
// sparse updates, not removals. All distances are runtime Y-up meters.
struct DogState {
    std::uint16_t id = 0;
    std::uint8_t asset = 0;
    glm::vec3 feet{0};
    float yaw = 0, phase = 0, walkSeconds = 0, attackSeconds = 0, health = 1;
    bool walking = false, running = false, attacking = false, dead = false;
};
struct DogFrame { std::uint64_t tick = 0; std::vector<DogState> dogs; };
} // namespace multiplayer
