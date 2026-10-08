#include "shader_program.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace viewer {
namespace {
GLuint compileShader(GLenum type, const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open shader: " + path.string());
    std::ostringstream contents;
    contents << stream.rdbuf();
    const std::string text = contents.str();
    const char* source = text.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = 0, size = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
    std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0');
    if (size > 1) {
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        std::cerr << "Shader " << path << ":\n" << log << '\n';
    }
    if (!success) {
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed: " + path.string() + "\n" + log);
    }
    return shader;
}
} // namespace

GLuint createShaderProgram(const std::filesystem::path& vertexPath,
                           const std::filesystem::path& fragmentPath) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexPath);
    GLuint fragment = 0, program = 0;
    try {
        fragment = compileShader(GL_FRAGMENT_SHADER, fragmentPath);
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            throw std::runtime_error("Shader linking failed (" + vertexPath.string() + "): " + log);
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
} // namespace viewer
