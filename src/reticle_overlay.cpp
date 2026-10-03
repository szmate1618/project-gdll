#include "reticle_overlay.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace viewer {
namespace {

GLuint compileShader(GLenum type, const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open reticle shader: " + path.string());
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string source = contents.str();
    const char* text = source.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Reticle shader compilation failed (" + path.string() + "): " + log);
    }
    return shader;
}

GLuint createProgram(const std::filesystem::path& directory) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, directory / "reticle.vert");
    GLuint fragment = 0, program = 0;
    try {
        fragment = compileShader(GL_FRAGMENT_SHADER, directory / "reticle.frag");
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            throw std::runtime_error("Reticle shader linking failed (" + directory.string() + "): " + log);
        }
    } catch (...) {
        if (program) glDeleteProgram(program);
        if (fragment) glDeleteShader(fragment);
        glDeleteShader(vertex);
        throw;
    }
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

int shaderMode(Camera::ViewMode mode) {
    switch (mode) {
        case Camera::ViewMode::original: return 0;
        case Camera::ViewMode::zoom10: return 1;
        case Camera::ViewMode::zoom1: return 2;
    }
    return 0;
}

}  // namespace

ReticleOverlay::ReticleOverlay(const std::filesystem::path& shaderDirectory) {
    GLint previousVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    try {
        program_ = createProgram(shaderDirectory);
        modeLocation_ = glGetUniformLocation(program_, "uMode");
        aspectLocation_ = glGetUniformLocation(program_, "uAspect");
        glGenVertexArrays(1, &vao_);
    } catch (...) {
        glBindVertexArray(static_cast<GLuint>(previousVao));
        release();
        throw;
    }
    glBindVertexArray(static_cast<GLuint>(previousVao));
}

ReticleOverlay::~ReticleOverlay() { release(); }

void ReticleOverlay::release() noexcept {
    glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void ReticleOverlay::draw(Camera::ViewMode mode, int width, int height) {
    if (width <= 0 || height <= 0) return;

    GLint previousProgram = 0, previousVao = 0;
    GLint previousBlendSourceRgb = 0, previousBlendDestinationRgb = 0;
    GLint previousBlendSourceAlpha = 0, previousBlendDestinationAlpha = 0;
    GLboolean previousDepthWrite = GL_TRUE;
    const GLboolean previousDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean previousBlend = glIsEnabled(GL_BLEND);
    const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDestinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDestinationAlpha);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform1i(modeLocation_, shaderMode(mode));
    glUniform1f(aspectLocation_, static_cast<float>(width) / static_cast<float>(height));
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(static_cast<GLuint>(previousVao));
    glUseProgram(static_cast<GLuint>(previousProgram));
    if (previousDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(previousDepthWrite);
    if (previousBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(static_cast<GLenum>(previousBlendSourceRgb), static_cast<GLenum>(previousBlendDestinationRgb),
                        static_cast<GLenum>(previousBlendSourceAlpha), static_cast<GLenum>(previousBlendDestinationAlpha));
    if (previousCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

}  // namespace viewer
