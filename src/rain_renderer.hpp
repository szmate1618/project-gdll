#pragma once

#include <filesystem>
#include <GL/glcorearb.h>

#include "camera.hpp"
#include "night_lighting.hpp"

namespace viewer {

// One instanced batch of camera-facing streaks. Static seeds are wrapped through
// a world-space volume around the eyes by the vertex shader; no CPU simulation,
// particle collisions, textures, or depth writes. All distances are meters.
class RainRenderer {
public:
    explicit RainRenderer(const std::filesystem::path& shaderDirectory);
    ~RainRenderer();
    RainRenderer(const RainRenderer&) = delete;
    RainRenderer& operator=(const RainRenderer&) = delete;

    // Draw into the caller's framebuffer using its existing depth buffer.
    // Restores program, VAO, blend, depth, and culling state after the draw.
    void draw(const Camera& camera, float aspect, double seconds, bool night,
              const NightLighting& lighting);

private:
    void release() noexcept;
    static constexpr GLsizei streakCount = 3000;
    GLuint program_ = 0, vao_ = 0, seeds_ = 0;
    GLint viewLocation_, projectionLocation_, eyeLocation_, timeLocation_;
    GLint nightLocation_, directionLocation_, colorLocation_, coneLocation_;
};

} // namespace viewer
