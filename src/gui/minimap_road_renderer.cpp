#include "minimap_road_renderer.hpp"
#include "minimap_roads.hpp"
#include <array>
#include <limits>
#include <stdexcept>

namespace viewer {
MinimapRoadRenderer::MinimapRoadRenderer(const Model& model) {
    const auto roads = prepareMinimapRoads(model);
    if (roads.vertices.empty()) return;
    if (roads.vertices.size() > std::size_t(std::numeric_limits<GLint>::max()))
        throw std::runtime_error("Minimap road geometry exceeds OpenGL draw limits");
    std::vector<VisibilityBounds> bounds;
    bounds.reserve(roads.batches.size()); ranges_.reserve(roads.batches.size());
    for (const auto& batch : roads.batches) {
        bounds.push_back({{batch.minimum.x, 0, batch.minimum.y}, {batch.maximum.x, 0, batch.maximum.y}});
        ranges_.push_back({GLint(batch.first), GLsizei(batch.count)});
    }
    index_ = VisibilityIndex(bounds);
    visible_.reserve(ranges_.size()); first_.reserve(ranges_.size()); count_.reserve(ranges_.size());
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, roads.vertices.size() * sizeof(glm::vec2), roads.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
}
MinimapRoadRenderer::~MinimapRoadRenderer() {
    glDeleteVertexArrays(1, &vao_); glDeleteBuffers(1, &buffer_);
    glDeleteFramebuffers(1, &multisampleFramebuffer_); glDeleteRenderbuffers(1, &multisampleColor_);
    glDeleteFramebuffers(1, &resolvedFramebuffer_); glDeleteTextures(1, &resolvedColor_);
}
void MinimapRoadRenderer::resizeTarget(glm::ivec2 size) {
    if (size == size_) return;
    if (!multisampleFramebuffer_) {
        glGenFramebuffers(1, &multisampleFramebuffer_); glGenRenderbuffers(1, &multisampleColor_);
        glGenFramebuffers(1, &resolvedFramebuffer_); glGenTextures(1, &resolvedColor_);
    }
    GLint renderbuffer;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, size.x, size.y);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, multisampleFramebuffer_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Incomplete minimap 4x MSAA framebuffer");
    glBindTexture(GL_TEXTURE_2D, resolvedColor_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolvedFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolvedColor_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Incomplete resolved minimap road framebuffer");
    size_ = size;
}
GLuint MinimapRoadRenderer::render(glm::vec2 center, float spanMeters, glm::ivec2 size) {
    if (!vao_ || size.x <= 0 || size.y <= 0) return 0;
    if (!queried_ || center != lastCenter_ || spanMeters != lastSpan_) {
        const auto minimum = center - spanMeters * .5f, maximum = center + spanMeters * .5f;
        index_.query(VisibilityBounds{{minimum.x, 0, minimum.y}, {maximum.x, 0, maximum.y}}, visible_);
        first_.clear(); count_.clear();
        for (const auto id : visible_) { first_.push_back(ranges_[id].first); count_.push_back(ranges_[id].count); }
        lastCenter_ = center; lastSpan_ = spanMeters; queried_ = true;
    }
    if (first_.empty()) return 0;
    GLint drawFramebuffer, readFramebuffer;
    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
    resizeTarget(size);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, multisampleFramebuffer_);
    glViewport(0, 0, size.x, size.y);
    glDisable(GL_SCISSOR_TEST);
    // Zero premultiplied color/alpha; resolve averages covered road samples
    // with transparency, so compositing preserves the satellite underneath.
    constexpr std::array<GLfloat, 4> transparent{{0, 0, 0, 0}};
    glClearBufferfv(GL_COLOR, 0, transparent.data());
    glBindVertexArray(vao_);
    glMultiDrawArrays(GL_TRIANGLES, first_.data(), count_.data(), GLsizei(first_.size()));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, multisampleFramebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolvedFramebuffer_);
    glBlitFramebuffer(0, 0, size.x, size.y, 0, 0, size.x, size.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return resolvedColor_;
}
} // namespace viewer
