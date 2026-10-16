#pragma once

#ifdef _WIN32
#include <GL/glew.h>
#include <stdexcept>
#include <string>
#else
#include <GL/glcorearb.h>
#endif

namespace viewer {
// Call once after making a window's OpenGL context current.
inline void initializeOpenGL() {
#ifdef _WIN32
    glewExperimental = GL_TRUE;
    const auto result = glewInit();
    if (result != GLEW_OK)
        throw std::runtime_error("Cannot load OpenGL: " + std::string(reinterpret_cast<const char*>(glewGetErrorString(result))));
    // GLEW's legacy extension probe can leave GL_INVALID_ENUM in a core context.
    glGetError();
    if (!GLEW_VERSION_3_3) throw std::runtime_error("OpenGL 3.3 or newer is required");
#endif
}
}
