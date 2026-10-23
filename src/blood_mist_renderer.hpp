#pragma once

#include "blood_mist.hpp"
#include "camera.hpp"
#include "opengl.hpp"
#include <filesystem>

namespace viewer {
// One shared alpha texture and one instanced camera-facing quad per impact.
class BloodMistRenderer {
public:
    explicit BloodMistRenderer(const std::filesystem::path& shaders);
    ~BloodMistRenderer();
    BloodMistRenderer(const BloodMistRenderer&) = delete;
    BloodMistRenderer& operator=(const BloodMistRenderer&) = delete;
    // Restores every GL state modified by this pass; never writes scene depth.
    void draw(const Camera& camera, float aspect, std::vector<BloodMistBurst> bursts, glm::vec3 ambient);
private:
    void release() noexcept;
    GLuint program_ = 0, vao_ = 0, instances_ = 0, texture_ = 0;
    GLint view_, projection_, textureLocation_, color_;
};
} // namespace viewer
