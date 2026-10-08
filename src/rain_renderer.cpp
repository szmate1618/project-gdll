#include "rain_renderer.hpp"
#include "shader_program.hpp"

#include <array>
#include <cmath>
#include <random>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {

RainRenderer::RainRenderer(const std::filesystem::path& shaderDirectory) {
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    try {
        program_ = createShaderProgram(shaderDirectory / "rain.vert", shaderDirectory / "rain.frag");
        viewLocation_ = glGetUniformLocation(program_, "uView");
        projectionLocation_ = glGetUniformLocation(program_, "uProjection");
        eyeLocation_ = glGetUniformLocation(program_, "uEye");
        timeLocation_ = glGetUniformLocation(program_, "uTime");
        nightLocation_ = glGetUniformLocation(program_, "uNight");
        directionLocation_ = glGetUniformLocation(program_, "uFlashlightDirection");
        colorLocation_ = glGetUniformLocation(program_, "uFlashlightColor");
        coneLocation_ = glGetUniformLocation(program_, "uFlashlightCone");
        std::mt19937 random(0x7261696e);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::array<glm::vec4, streakCount> seeds;
        for (auto& seed : seeds) seed = {unit(random), unit(random), unit(random), 0.85f + 0.3f * unit(random)};
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &seeds_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, seeds_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(seeds), seeds.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
        glVertexAttribDivisor(0, 1);
    } catch (...) {
        glBindVertexArray(static_cast<GLuint>(previousVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
        release();
        throw;
    }
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}

RainRenderer::~RainRenderer() { release(); }

void RainRenderer::release() noexcept {
    glDeleteBuffers(1, &seeds_);
    glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void RainRenderer::draw(const Camera& camera, float aspect, double seconds, bool night,
                        const NightLighting& lighting) {
    GLint program = 0, vao = 0, sourceRgb = 0, destRgb = 0, sourceAlpha = 0, destAlpha = 0;
    GLint equationRgb = 0, equationAlpha = 0;
    GLboolean depthWrite = GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &destRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &destAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    const auto depthTest = glIsEnabled(GL_DEPTH_TEST);
    const auto blend = glIsEnabled(GL_BLEND);
    const auto cull = glIsEnabled(GL_CULL_FACE);

    glUseProgram(program_);
    const auto view = camera.view();
    const auto projection = camera.projection(aspect);
    const auto eye = camera.position();
    const auto direction = camera.direction();
    glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(eyeLocation_, 1, glm::value_ptr(eye));
    glUniform1f(timeLocation_, static_cast<float>(seconds));
    glUniform1i(nightLocation_, night);
    glUniform3fv(directionLocation_, 1, glm::value_ptr(direction));
    glUniform3fv(colorLocation_, 1, glm::value_ptr(lighting.flashlightColor));
    glUniform3f(coneLocation_, lighting.flashlightRange,
                std::cos(glm::radians(lighting.innerAngle)), std::cos(glm::radians(lighting.outerAngle)));
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    // Sparse reflections add light without requiring a sorted particle list.
    // Preserve framebuffer alpha and the scene depth buffer for overlays.
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, streakCount);

    glBindVertexArray(static_cast<GLuint>(vao));
    glUseProgram(static_cast<GLuint>(program));
    glDepthMask(depthWrite);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glBlendFuncSeparate(static_cast<GLenum>(sourceRgb), static_cast<GLenum>(destRgb),
                        static_cast<GLenum>(sourceAlpha), static_cast<GLenum>(destAlpha));
    glBlendEquationSeparate(static_cast<GLenum>(equationRgb), static_cast<GLenum>(equationAlpha));
}

} // namespace viewer
