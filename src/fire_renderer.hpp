#pragma once
#include "fire_effects.hpp"
#include "camera.hpp"
#include "opengl.hpp"
#include <filesystem>

namespace viewer {
class FireRenderer {
public:
    explicit FireRenderer(const std::filesystem::path& shaders);
    ~FireRenderer();
    FireRenderer(const FireRenderer&) = delete;
    FireRenderer& operator=(const FireRenderer&) = delete;
    void draw(const Camera& camera, float aspect, const FireEffects& fire, double seconds);
private:
    void release() noexcept;
    struct Instance { glm::vec4 pointAge; glm::vec2 sizeSeed; };
    std::array<Instance,FireEffects::capacity> scratch_{};
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLint projection_, right_;
};
} // namespace viewer
