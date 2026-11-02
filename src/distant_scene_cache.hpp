#pragma once

#include "camera.hpp"
#include "opengl.hpp"
#include "visibility.hpp"
#include <cstddef>
#include <filesystem>
#include <functional>

namespace viewer {

struct DistantCacheStats {
    bool active = false, refreshed = false;
    std::size_t drawCalls = 0;
};

// One 15 Hz color capture, displayed on a rectangle at a 1 km proxy depth.
// New screen coverage waits for the next refresh. The rectangle is a background:
// depth is used only while capturing, never sampled or published to the scene.
// Capture depth follows the active scene convention.
// Owns GL objects; destroy while its creating context is current.
class DistantSceneCache {
public:
    explicit DistantSceneCache(const std::filesystem::path& shaders);
    ~DistantSceneCache();
    DistantSceneCache(const DistantSceneCache&) = delete;
    DistantSceneCache& operator=(const DistantSceneCache&) = delete;
    void invalidate() { valid_ = false; }
    void setDistance(float meters) { if (meters != proxyDistance_) { proxyDistance_ = meters; invalidate(); } }
    // Keep projection conventional for rectangle/culling calculations. The draw
    // callback supplies the GPU projection matching the active scene depth.
    void update(const Camera& camera, const glm::mat4& projection, int width, int height, double seconds,
                glm::vec3 background, const std::function<void(const glm::mat4&, const glm::mat4&)>& draw);
    void composite(const Camera& camera, const glm::mat4& projection);
    glm::vec3 origin() const { return origin_; }
    DistantCacheStats stats;
    static bool distant(const VisibilityBounds& bounds, glm::vec3 eye, float distance);

private:
    void release() noexcept;
    void resize(int width, int height);
    GLuint framebuffer_ = 0, color_ = 0, depth_ = 0, program_ = 0, vao_ = 0;
    GLint transform_ = -1;
    int width_ = 0, height_ = 0;
    bool valid_ = false, reversedDepth_ = false;
    float proxyDistance_ = 1000;
    double refreshedAt_ = 0;
    glm::vec3 origin_{0};
    glm::mat4 worldQuad_{1};
};
} // namespace viewer
