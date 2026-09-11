#pragma once

#include <filesystem>
#include <vector>

#include <GL/glcorearb.h>
#include <glm/glm.hpp>

#include "camera.hpp"

namespace viewer {

// Consecutive pairs form independent world-space line segments.
struct DebugVertex {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
};

// Draw after Renderer::render and before Renderer::present. Uses the current
// framebuffer and draws through geometry so nearby collision shapes stay clear.
class CollisionDebug {
public:
    explicit CollisionDebug(const std::filesystem::path& shaderDirectory);
    ~CollisionDebug();
    CollisionDebug(const CollisionDebug&) = delete;
    CollisionDebug& operator=(const CollisionDebug&) = delete;

    void draw(const Camera& camera, const std::vector<DebugVertex>& lines,
              int width, int height);

private:
    void release() noexcept;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint viewProjectionLocation_ = -1;
};

} // namespace viewer
