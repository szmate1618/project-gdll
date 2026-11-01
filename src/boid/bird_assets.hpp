#pragma once
#include "model.hpp"
#include <array>

namespace viewer {
struct BirdAnimationSample {
    glm::vec4 position{0}, normal{0};
};
struct BirdMesh {
    std::vector<glm::vec2> uv;
    std::vector<std::uint32_t> indices;
    // Frame-major samples, shared by every bird. Asset X/Y/Z = right/up/forward.
    std::vector<BirdAnimationSample> samples;
};
struct BirdAssets {
    ImageData atlas;
    std::array<BirdMesh, 3> meshes;
    std::size_t frameCount = 0;
    float duration = 0, radius = 0;
};
// The tracked descriptor includes the 16px atlas and baked Flapping clip.
BirdAssets loadBirdAssets(const std::filesystem::path& path);
// Screen-size policy: full mesh above 48 pixels, middle above 16 pixels.
std::size_t birdDetailLevel(float distance, float radius, float projectionY, int viewportHeight);
}
