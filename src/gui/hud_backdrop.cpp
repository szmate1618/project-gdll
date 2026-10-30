#include "hud_backdrop.hpp"
#include "command_wheel.hpp"
#include "shader_program.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace viewer {
namespace {
constexpr int sceneWidth = 128, sceneHeight = 72;

// The outer HUD state guard preserves unit zero. This pass also borrows unit one.
class TextureUnitOne {
public:
    TextureUnitOne() {
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler_);
    }
    ~TextureUnitOne() {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_));
        glBindSampler(1, static_cast<GLuint>(sampler_));
        glActiveTexture(GL_TEXTURE0);
    }
private:
    GLint texture_ = 0, sampler_ = 0;
};

void allocateTarget(GLuint& framebuffer, GLuint& texture, GLint format,
                    int width, int height, GLenum channels, GLenum type) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, channels, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Cannot create adaptive HUD backdrop framebuffer");
}
} // namespace

HudBackdrop::HudBackdrop(const std::filesystem::path& directory) {
    try {
        updateProgram_ = createShaderProgram(directory / "hud-backdrop-update.vert", directory / "hud-backdrop-update.frag");
        drawProgram_ = createShaderProgram(directory / "hud-backdrop.vert", directory / "hud-backdrop.frag");
        updateBounds_ = glGetUniformLocation(updateProgram_, "uBounds");
        updateViewport_ = glGetUniformLocation(updateProgram_, "uViewport");
        updateDelta_ = glGetUniformLocation(updateProgram_, "uDeltaSeconds");
        updateHistoryValid_ = glGetUniformLocation(updateProgram_, "uHistoryValid");
        glUseProgram(updateProgram_);
        glUniform1i(glGetUniformLocation(updateProgram_, "uScene"), 0);
        glUniform1i(glGetUniformLocation(updateProgram_, "uHistory"), 1);
        constexpr float innerRadius = CommandSectorGeometry::innerRadius / CommandSectorGeometry::designSize;
        constexpr float outerRadius = CommandSectorGeometry::outerRadius / CommandSectorGeometry::designSize;
        glUniform2f(glGetUniformLocation(updateProgram_, "uSectorRadii"), innerRadius, outerRadius);
        drawBounds_ = glGetUniformLocation(drawProgram_, "uBounds");
        drawViewport_ = glGetUniformLocation(drawProgram_, "uViewport");
        drawPadding_ = glGetUniformLocation(drawProgram_, "uPadding");
        drawColor_ = glGetUniformLocation(drawProgram_, "uColor");
        drawSectorPass_ = glGetUniformLocation(drawProgram_, "uSectorPass");
        glUseProgram(drawProgram_);
        glUniform1i(glGetUniformLocation(drawProgram_, "uOpacity"), 0);
        glUniform2f(glGetUniformLocation(drawProgram_, "uSectorRadii"), innerRadius, outerRadius);
        glUniform1f(glGetUniformLocation(drawProgram_, "uSectorSegments"), static_cast<float>(CommandSectorGeometry::segments));
        glGenVertexArrays(1, &vao_);
        glActiveTexture(GL_TEXTURE0);
        // nullptr must denote an empty allocation, even if the caller has an
        // upload PBO bound. The enclosing HUD guard restores unpack state.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        allocateTarget(sceneFramebuffer_, sceneTexture_, GL_RGBA8, sceneWidth, sceneHeight, GL_RGBA, GL_UNSIGNED_BYTE);
        for (std::size_t i = 0; i < historyTextures_.size(); ++i)
            allocateTarget(historyFramebuffers_[i], historyTextures_[i], GL_R16F,
                           static_cast<int>(maxGroups), 1, GL_RED, GL_FLOAT);
    } catch (...) {
        release();
        throw;
    }
}

HudBackdrop::~HudBackdrop() { release(); }

void HudBackdrop::release() {
    glDeleteFramebuffers(static_cast<GLsizei>(historyFramebuffers_.size()), historyFramebuffers_.data());
    glDeleteTextures(static_cast<GLsizei>(historyTextures_.size()), historyTextures_.data());
    glDeleteFramebuffers(1, &sceneFramebuffer_);
    glDeleteTextures(1, &sceneTexture_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(updateProgram_);
    glDeleteProgram(drawProgram_);
}

void HudBackdrop::reset() { historyValid_ = false; }

void HudBackdrop::draw(const Bounds& bounds, int width, int height, float deltaSeconds, glm::vec3 encodedColor) {
    if (width <= 0 || height <= 0) { reset(); return; }
    if (width != width_ || height != height_) reset();
    width_ = width;
    height_ = height;
    if (std::none_of(bounds.begin(), bounds.end(), [](const glm::vec4& rectangle) {
            return rectangle.z > 0 && rectangle.w > 0;
        })) { reset(); return; }

    TextureUnitOne restoreUnitOne;
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    // The caller may have a different read framebuffer bound. Capture the
    // actual HUD destination before either backdrops or HUD content are drawn.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(target));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFramebuffer_);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, width, height, 0, 0, sceneWidth, sceneHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    const auto nextHistory = 1 - historyIndex_;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, historyFramebuffers_[nextHistory]);
    glViewport(0, 0, static_cast<GLsizei>(maxGroups), 1);
    glDisable(GL_BLEND);
    glBindVertexArray(vao_);
    glUseProgram(updateProgram_);
    glUniform4fv(updateBounds_, static_cast<GLsizei>(maxGroups), glm::value_ptr(bounds.front()));
    glUniform2f(updateViewport_, static_cast<float>(width), static_cast<float>(height));
    glUniform1f(updateDelta_, std::isfinite(deltaSeconds) ? std::max(0.f, deltaSeconds) : 0.f);
    glUniform1i(updateHistoryValid_, historyValid_);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture_);
    glActiveTexture(GL_TEXTURE1);
    glBindSampler(1, 0);
    glBindTexture(GL_TEXTURE_2D, historyTextures_[historyIndex_]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    historyIndex_ = nextHistory;
    historyValid_ = true;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(target));
    glViewport(0, 0, width, height);
    glUseProgram(drawProgram_);
    glUniform4fv(drawBounds_, static_cast<GLsizei>(maxGroups), glm::value_ptr(bounds.front()));
    glUniform2f(drawViewport_, static_cast<float>(width), static_cast<float>(height));
    const float scale = std::min(static_cast<float>(width) / 1280.f, static_cast<float>(height) / 720.f);
    glUniform1f(drawPadding_, 6.f * scale);
    glUniform3fv(drawColor_, 1, glm::value_ptr(encodedColor));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, historyTextures_[historyIndex_]);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glUniform1i(drawSectorPass_, GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(panelGroups));
    if (std::any_of(bounds.begin() + panelGroups, bounds.end(), [](const glm::vec4& rectangle) {
            return rectangle.z > 0 && rectangle.w > 0;
        })) {
        glUniform1i(drawSectorPass_, GL_TRUE);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 2 * (CommandSectorGeometry::segments + 1),
                              static_cast<GLsizei>(sectorGroups));
    }
}

} // namespace viewer
