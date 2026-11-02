#pragma once

#include "camera.hpp"
#include "opengl.hpp"
#include "visibility.hpp"
#include <filesystem>
#include <functional>
#include <vector>

namespace viewer {

struct DistantCacheStats {
    bool refreshed = false;
    std::size_t tiles = 0, patches = 0, drawCalls = 0;
};

// Directional tiles keep their original texels across turns. Translation is
// deliberately held until the next 15 Hz refresh; no repeated image resampling.
// Owns GL objects; destroy while its creating context is current.
class DistantSceneCache {
public:
    explicit DistantSceneCache(const std::filesystem::path& shaders);
    ~DistantSceneCache();
    DistantSceneCache(const DistantSceneCache&) = delete;
    DistantSceneCache& operator=(const DistantSceneCache&) = delete;
    void invalidate() { valid_ = false; }
    void update(const Camera& camera, const glm::mat4& projection, int height, double seconds,
                glm::vec3 background, const std::function<void(const glm::mat4&, const glm::mat4&)>& draw);
    void composite(const Camera& camera, const glm::mat4& projection);
    glm::vec3 origin() const { return origin_; }
    DistantCacheStats stats;
    static bool distant(const VisibilityBounds& bounds, glm::vec3 eye, float distance);

private:
    void release() noexcept;
    void resize(int resolution);
    GLuint framebuffer_ = 0, color_ = 0, depth_ = 0, program_ = 0, vao_ = 0;
    GLint inverseView_ = -1, projection_ = -1, inverseProjection_ = -1;
    int resolution_ = 0, tilesPerFace_ = 0;
    std::vector<bool> tiles_;
    bool valid_ = false;
    double refreshedAt_ = 0;
    glm::vec3 origin_{0};
};
} // namespace viewer
