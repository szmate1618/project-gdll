#pragma once

#include "camera.hpp"
#include "opengl.hpp"
#include <filesystem>
#include <vector>

namespace viewer {

// Consecutive endpoints form world-space intersection segments. Draws a single
// batch into the camera framebuffer, against its existing scene depth buffer.
class BindingRenderer {
public:
    explicit BindingRenderer(const std::filesystem::path& shaderDirectory);
    ~BindingRenderer();
    BindingRenderer(const BindingRenderer&) = delete;
    BindingRenderer& operator=(const BindingRenderer&) = delete;

    void draw(const Camera& camera, float aspect, const std::vector<glm::vec3>& endpoints);

private:
    struct Vertex {
        glm::vec3 position;
        glm::vec3 color;
    };
    void release() noexcept;
    GLuint program_ = 0, vao_ = 0, vertices_ = 0;
    GLint viewProjectionLocation_ = -1;
    GLfloat lineWidth_ = 1;
    std::vector<Vertex> batch_;
};

} // namespace viewer
