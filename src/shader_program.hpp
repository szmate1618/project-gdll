#pragma once

#include <filesystem>
#include <string_view>
#include "opengl.hpp"

namespace viewer {

// Compile and link a vertex/fragment pair. Requires a current GL context;
// the caller owns the returned program. Failures release intermediate objects.
// Optional definitions are inserted after each source's leading #version line.
GLuint createShaderProgram(const std::filesystem::path& vertexPath,
                           const std::filesystem::path& fragmentPath,
                           std::string_view definitions = {});

} // namespace viewer
