#pragma once

#include "blood_mist.hpp"
#include "blood_particles.hpp"
#include "camera.hpp"
#include "opengl.hpp"
#include <filesystem>

namespace viewer {
// Shared texture/pass for mist, ballistic droplets and static floor splats.
class BloodMistRenderer {
public:
    explicit BloodMistRenderer(const std::filesystem::path& shaders);
    ~BloodMistRenderer();
    BloodMistRenderer(const BloodMistRenderer&) = delete;
    BloodMistRenderer& operator=(const BloodMistRenderer&) = delete;
    // Restores every GL state modified by this pass; never writes scene depth.
    void draw(const Camera& camera, float aspect, const std::vector<BloodMistBurst>& bursts, glm::vec3 ambient,
              const BloodParticles* particles = nullptr, double seconds = 0);
private:
    struct Instance {
        glm::vec4 centerSize;
        glm::vec2 rotationOpacity;
        glm::vec4 velocityAge, normalKind; // Kind: mist 0, droplet 1, stain 2.
        float distance;
    };
    static constexpr std::size_t capacity = BloodMist::capacity+BloodParticles::particleCapacity+BloodParticles::stainCapacity;
    std::array<Instance,capacity> scratch_{};
    void release() noexcept;
    GLuint program_ = 0, vao_ = 0, instances_ = 0, texture_ = 0;
    GLint view_, projection_, textureLocation_, color_;
};
} // namespace viewer
