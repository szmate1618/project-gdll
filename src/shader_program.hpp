#pragma once

#include <filesystem>
#include <GL/glcorearb.h>

namespace viewer {

// Compile and link a vertex/fragment pair. Requires a current GL context;
// the caller owns the returned program. Failures release intermediate objects.
GLuint createShaderProgram(const std::filesystem::path& vertexPath,
                           const std::filesystem::path& fragmentPath);

} // namespace viewer
