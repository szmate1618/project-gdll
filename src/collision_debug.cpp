#include "collision_debug.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
GLuint compileShader(GLenum type, const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open collision debug shader: " + path.string());
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
        throw std::runtime_error("Collision debug shader compilation failed (" + path.string() + "): " + log);
    }
    return shader;
}

GLuint createProgram(const std::filesystem::path& directory) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, directory / "debug.vert");
    GLuint fragment = 0, program = 0;
    try {
        fragment = compileShader(GL_FRAGMENT_SHADER, directory / "debug.frag");
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
            throw std::runtime_error("Collision debug shader linking failed (" + directory.string() + "): " + log);
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
} // namespace

CollisionDebug::CollisionDebug(const std::filesystem::path& shaderDirectory) {
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    try {
        program_ = createProgram(shaderDirectory);
        viewProjectionLocation_ = glGetUniformLocation(program_, "uViewProjection");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex),
                              reinterpret_cast<const void*>(offsetof(DebugVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex),
                              reinterpret_cast<const void*>(offsetof(DebugVertex, color)));
    } catch (...) {
        glBindVertexArray(static_cast<GLuint>(previousVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
        release();
        throw;
    }
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}

CollisionDebug::~CollisionDebug() { release(); }

void CollisionDebug::release() noexcept {
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
}

void CollisionDebug::draw(const Camera& camera, const std::vector<DebugVertex>& lines,
                          int width, int height) {
    if (lines.empty() || width <= 0 || height <= 0) return;
    if (lines.size() % 2 != 0)
        throw std::runtime_error("Collision debug lines require pairs of endpoints");
    if (lines.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        lines.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(DebugVertex))
        throw std::runtime_error("Too many collision debug line vertices");

    GLint previousProgram = 0, previousVao = 0, previousBuffer = 0;
    GLboolean previousDepthWrite = GL_TRUE;
    const GLboolean previousDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean previousBlend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glUseProgram(program_);
    const glm::mat4 viewProjection = camera.projection(static_cast<float>(width) / static_cast<float>(height)) * camera.view();
    glUniformMatrix4fv(viewProjectionLocation_, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(DebugVertex)),
                 lines.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));

    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
    glUseProgram(static_cast<GLuint>(previousProgram));
    if (previousDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(previousDepthWrite);
    if (previousBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

} // namespace viewer
