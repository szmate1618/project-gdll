#pragma once

#include "camera.hpp"
#include "bird_assets.hpp"
#include "opengl.hpp"

#include <filesystem>
#include <vector>

namespace viewer {

struct FlockBird;

struct BirdDrawStats {
    std::array<std::size_t, 3> instances{};
    std::size_t triangles = 0;
};

// Shared pigeon meshes with GPU vertex animation and screen-size LoD.
// Positions and velocities use the runtime's Y-up world meters. This renderer
// owns GPU resources only; flock movement and contacts stay in BirdFlocks.
class BirdRenderer {
public:
    BirdRenderer(const std::filesystem::path& shaderDirectory, const std::filesystem::path& assetPath);
    ~BirdRenderer();
    BirdRenderer(const BirdRenderer&) = delete;
    BirdRenderer& operator=(const BirdRenderer&) = delete;

    // At most three instanced draws; restores every GL state changed here.
    // Tint multiplies the existing display-space palette and directional shade.
    BirdDrawStats draw(const Camera& camera, float aspect, double seconds,
                       const std::vector<FlockBird>& birds, const glm::vec3& tint = glm::vec3(1.0f));

private:
    void release() noexcept;
    struct Batch {
        GLuint vao = 0, uv = 0, indices = 0, instances = 0, samples = 0, animation = 0;
        GLsizei indexCount = 0;
        GLint vertexCount = 0;
    };
    GLuint program_ = 0, atlas_ = 0;
    std::array<Batch, 3> batches_{};
    GLint projectionViewLocation_ = -1, frameLocation_ = -1, frameCountLocation_ = -1, vertexCountLocation_ = -1;
    GLint dayNightTintLocation_ = -1;
    float radius_ = 0, duration_ = 0;
    std::size_t frameCount_ = 0;
    std::array<std::vector<FlockBird>, 3> visible_;
};

} // namespace viewer
