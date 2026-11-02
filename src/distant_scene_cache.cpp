#include "distant_scene_cache.hpp"
#include "shader_program.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
constexpr int tileSize = 256;
constexpr float nearPlane = 100, farPlane = 20000;
const glm::vec3 directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
const glm::vec3 up[] = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
}

DistantSceneCache::DistantSceneCache(const std::filesystem::path& shaders) {
    try {
        program_ = createShaderProgram(shaders / "distant_cache.vert", shaders / "distant_cache.frag");
        inverseView_ = glGetUniformLocation(program_, "uInverseView");
        projection_ = glGetUniformLocation(program_, "uProjection");
        inverseProjection_ = glGetUniformLocation(program_, "uInverseProjection");
        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "uColor"), 0);
        glUniform1i(glGetUniformLocation(program_, "uDepth"), 1);
        glGenVertexArrays(1, &vao_);
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &color_);
        glGenTextures(1, &depth_);
    } catch (...) { release(); throw; }
}
DistantSceneCache::~DistantSceneCache() { release(); }
void DistantSceneCache::release() noexcept {
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteTextures(1, &color_); glDeleteTextures(1, &depth_);
    glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}

bool DistantSceneCache::distant(const VisibilityBounds& bounds, glm::vec3 eye, float distance) {
    const auto delta = eye - glm::clamp(eye, bounds.minimum, bounds.maximum);
    return glm::dot(delta, delta) >= distance * distance;
}

void DistantSceneCache::resize(int resolution) {
    if (resolution_ == resolution) return;
    resolution_ = resolution;
    tilesPerFace_ = resolution / tileSize;
    tiles_.assign(static_cast<std::size_t>(6 * tilesPerFace_ * tilesPerFace_), false);
    for (const auto texture : {color_, depth_}) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        for (int face = 0; face < 6; ++face)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                         texture == color_ ? GL_RGBA8 : GL_DEPTH_COMPONENT24,
                         resolution, resolution, 0, texture == color_ ? GL_RGBA : GL_DEPTH_COMPONENT,
                         texture == color_ ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT, nullptr);
    }
    invalidate();
}

void DistantSceneCache::update(const Camera& camera, const glm::mat4& projection, int height, double seconds,
                             glm::vec3 background,
                             const std::function<void(const glm::mat4&, const glm::mat4&)>& draw) {
    // Match the screen's angular texel density at the center of a cube face.
    // Scope views bypass this cache instead of allocating enormous textures.
    resize(std::clamp(static_cast<int>(std::ceil(height * projection[1][1] / tileSize)) * tileSize, tileSize, 4096));
    stats = {};
    if (!valid_ || seconds < refreshedAt_ || seconds - refreshedAt_ >= 1.0 / 15.0 - 1e-9 ||
        glm::distance(camera.position(), origin_) > 20) {
        origin_ = camera.position(); refreshedAt_ = seconds; valid_ = true;
        std::fill(tiles_.begin(), tiles_.end(), false);
        stats.refreshed = true;
    }
    const Frustum visible(projection * glm::mat4(glm::mat3(camera.view())));
    glActiveTexture(GL_TEXTURE0);
    // A previous composite binds these textures for sampling. Unbind before
    // attaching either face to avoid framebuffer/texture feedback.
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glEnable(GL_SCISSOR_TEST);
    for (int face = 0; face < 6; ++face) {
        const auto rotation = glm::lookAt(glm::vec3(0), directions[face], up[face]);
        const auto inverseRotation = glm::transpose(glm::mat3(rotation));
        const auto view = rotation * glm::translate(glm::mat4(1), -origin_);
        for (int y = 0; y < tilesPerFace_; ++y) for (int x = 0; x < tilesPerFace_; ++x) {
            const float left = -1 + 2.0f * x / tilesPerFace_, bottom = -1 + 2.0f * y / tilesPerFace_;
            const float size = 2.0f / tilesPerFace_;
            VisibilityBounds bounds{glm::vec3(1e9f), glm::vec3(-1e9f)};
            for (int corner = 0; corner < 4; ++corner) {
                const auto point = inverseRotation * glm::vec3(left + ((corner & 1) ? size : 0),
                    bottom + ((corner & 2) ? size : 0), -1) * 1000.0f;
                bounds.minimum = glm::min(bounds.minimum, point);
                bounds.maximum = glm::max(bounds.maximum, point);
            }
            if (!visible.intersects(bounds.minimum, bounds.maximum)) continue;
            ++stats.tiles;
            const auto index = static_cast<std::size_t>((face * tilesPerFace_ + y) * tilesPerFace_ + x);
            if (tiles_[index]) continue;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, color_, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, depth_, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Incomplete distant cache framebuffer");
            glViewport(x * tileSize, y * tileSize, tileSize, tileSize);
            glScissor(x * tileSize, y * tileSize, tileSize, tileSize);
            glDepthMask(GL_TRUE);
            glClearColor(background.r, background.g, background.b, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // Crop projection as well as the viewport: its frustum queries only
            // submit geometry intersecting this newly exposed angular tile.
            const auto tileProjection = glm::frustum(left * nearPlane, (left + size) * nearPlane,
                bottom * nearPlane, (bottom + size) * nearPlane, nearPlane, farPlane);
            draw(view, tileProjection);
            tiles_[index] = true;
            ++stats.patches;
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

void DistantSceneCache::composite(const Camera& camera, const glm::mat4& projection) {
    glUseProgram(program_);
    glUniformMatrix3fv(inverseView_, 1, GL_FALSE, glm::value_ptr(glm::transpose(glm::mat3(camera.view()))));
    glUniformMatrix4fv(projection_, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform2f(inverseProjection_, 1 / projection[0][0], 1 / projection[1][1]);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP, color_);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_CUBE_MAP, depth_);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glBindVertexArray(vao_); glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthFunc(GL_LESS);
    glActiveTexture(GL_TEXTURE0);
}
} // namespace viewer
