#pragma once

#include "camera.hpp"
#include "opengl.hpp"

#include <filesystem>
#include <vector>

namespace viewer {

struct FlockBird;

// Three perpendicular, masked square cards per bird in one instanced draw.
// Positions and velocities use the runtime's Y-up world meters. This renderer
// owns GPU resources only; flock movement and contacts stay in BirdFlocks.
class BirdRenderer {
public:
    explicit BirdRenderer(const std::filesystem::path& shaderDirectory);
    ~BirdRenderer();
    BirdRenderer(const BirdRenderer&) = delete;
    BirdRenderer& operator=(const BirdRenderer&) = delete;

    // Draw into the caller's opaque framebuffer. Empty pixels are discarded;
    // silhouettes write depth. Restores every GL state changed by this draw.
    void draw(const Camera& camera, float aspect, double seconds,
              const std::vector<FlockBird>& birds);

private:
    void release() noexcept;
    GLuint program_ = 0, vao_ = 0, instances_ = 0;
    GLint projectionViewLocation_ = -1, timeLocation_ = -1;
    std::vector<FlockBird> visible_;
};

} // namespace viewer
