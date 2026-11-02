#pragma once
#include <glm/vec2.hpp>
#include <optional>

namespace multiplayer {
struct ClientDisplay {
    bool fullscreen = false;
    std::optional<glm::ivec2> resolution; // Fixed 3D pixels; HUD remains at framebuffer resolution.
    glm::ivec2 renderSize() const { return resolution.value_or(glm::ivec2(1280, 720)); }
};
} // namespace multiplayer
