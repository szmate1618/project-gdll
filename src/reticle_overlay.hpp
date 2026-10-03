#pragma once

#include <filesystem>

#include <GL/glcorearb.h>

#include "camera.hpp"

namespace viewer {

// A screen-space aiming aid drawn over the scene framebuffer. Normal view uses
// a compact crosshair; zoom modes add a circular scope and vignette.
class ReticleOverlay {
public:
    explicit ReticleOverlay(const std::filesystem::path& shaderDirectory);
    ~ReticleOverlay();
    ReticleOverlay(const ReticleOverlay&) = delete;
    ReticleOverlay& operator=(const ReticleOverlay&) = delete;

    void draw(Camera::ViewMode mode, int width, int height);

private:
    void release() noexcept;

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLint modeLocation_ = -1;
    GLint aspectLocation_ = -1;
};

}  // namespace viewer
