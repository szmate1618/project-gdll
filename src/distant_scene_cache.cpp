#include "distant_scene_cache.hpp"
#include "scene_depth.hpp"
#include "shader_program.hpp"
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
DistantSceneCache::DistantSceneCache(const std::filesystem::path& shaders) {
    try {
        program_ = createShaderProgram(shaders / "distant_cache.vert", shaders / "distant_cache.frag");
        transform_ = glGetUniformLocation(program_, "uTransform");
        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "uColor"), 0);
        glGenVertexArrays(1, &vao_);
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &color_);
        glGenRenderbuffers(1, &depth_);
    } catch (...) { release(); throw; }
}
DistantSceneCache::~DistantSceneCache() { release(); }
void DistantSceneCache::release() noexcept {
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteTextures(1, &color_); glDeleteRenderbuffers(1, &depth_);
    glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}

bool DistantSceneCache::distant(const VisibilityBounds& bounds, glm::vec3 eye, float distance) {
    const auto delta = eye - glm::clamp(eye, bounds.minimum, bounds.maximum);
    return glm::dot(delta, delta) >= distance * distance;
}

void DistantSceneCache::resize(int width, int height) {
    if (width_ == width && height_ == height && reversedDepth_ == sceneReversedDepth) return;
    width_ = width; height_ = height;
    reversedDepth_ = sceneReversedDepth;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, reversedDepth_ ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24,
                          width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Incomplete distant rectangle framebuffer");
    invalidate();
}

void DistantSceneCache::update(const Camera& camera, const glm::mat4& projection, int width, int height, double seconds,
                             glm::vec3 background,
                             const std::function<void(const glm::mat4&, const glm::mat4&)>& draw) {
    resize(width, height);
    stats = {}; stats.active = true;
    if (valid_ && seconds >= refreshedAt_ && seconds - refreshedAt_ < 1.0 / 15.0 - 1e-9 &&
        glm::distance(camera.position(), origin_) <= 20) return;
    origin_ = camera.position(); refreshedAt_ = seconds;
    // A rectangle matches this camera's image at capture time. Thereafter its
    // four corners follow the current view, accepting gaps outside the image.
    worldQuad_ = glm::inverse(camera.view());
    worldQuad_[0] *= 1000 / projection[0][0];
    worldQuad_[1] *= 1000 / projection[1][1];
    worldQuad_[2] *= 1000;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(reversedDepth_ ? GL_GREATER : GL_LESS);
    glClearDepth(reversedDepth_ ? 0 : 1);
    glClearColor(background.r, background.g, background.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw(camera.view(), projection);
    valid_ = true; stats.refreshed = true;
}

void DistantSceneCache::composite(const Camera& camera, const glm::mat4& projection) {
    const auto transform = projection * camera.view() * worldQuad_;
    glUseProgram(program_);
    glUniformMatrix4fv(transform_, 1, GL_FALSE, glm::value_ptr(transform));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, color_);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    // This is a background color rectangle. Its proxy depth must neither
    // occlude foreground geometry nor depend on the scene's depth convention.
    glDepthFunc(GL_ALWAYS); glDepthMask(GL_FALSE);
    glBindVertexArray(vao_); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDepthFunc(sceneReversedDepth ? GL_GREATER : GL_LESS); glDepthMask(GL_TRUE);
}
} // namespace viewer
