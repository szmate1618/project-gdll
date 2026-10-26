#include "renderer.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace viewer {
PresentationViewport Renderer::beginOverlay(int width, int height) {
    const auto viewport = presentationViewport(width_, height_, width, height);
    if (viewport.width <= 0 || viewport.height <= 0) return viewport;
    overlayActive_ = false;
    if (width != width_ || height != height_) {
        if (!overlayFramebuffer_) glGenFramebuffers(1, &overlayFramebuffer_);
        if (!overlayColor_) glGenRenderbuffers(1, &overlayColor_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, overlayFramebuffer_);
        if (width != overlayWidth_ || height != overlayHeight_) {
            glBindRenderbuffer(GL_RENDERBUFFER, overlayColor_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, overlayColor_);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Cannot create the GUI framebuffer at " +
                    std::to_string(width) + "x" + std::to_string(height));
            overlayWidth_ = width;
            overlayHeight_ = height;
        }
        glDisable(GL_SCISSOR_TEST);
        constexpr GLfloat black[]{0, 0, 0, 1};
        glClearBufferfv(GL_COLOR, 0, black);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
        const int bottom = height - viewport.y - viewport.height;
        glBlitFramebuffer(0, 0, width_, height_, viewport.x, bottom,
            viewport.x + viewport.width, bottom + viewport.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        overlayActive_ = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, overlayActive_ ? overlayFramebuffer_ : framebuffer_);
    glViewport(viewport.x, height - viewport.y - viewport.height, viewport.width, viewport.height);
    return viewport;
}

void Renderer::present(int width, int height) {
    const int sourceWidth = overlayActive_ ? overlayWidth_ : width_;
    const int sourceHeight = overlayActive_ ? overlayHeight_ : height_;
    if (width<=0 || height<=0 || sourceWidth<=0 || sourceHeight<=0) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, overlayActive_ ? overlayFramebuffer_ : framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const auto viewport = presentationViewport(sourceWidth, sourceHeight, width, height);
    const int y = height - viewport.y - viewport.height; // OpenGL has a bottom-left origin.
    if (viewport.width!=width || viewport.height!=height) {
        constexpr GLfloat black[]{0,0,0,1};
        glClearBufferfv(GL_COLOR,0,black);
    }
    glBlitFramebuffer(0, 0, sourceWidth, sourceHeight, viewport.x, y, viewport.x+viewport.width, y+viewport.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void Renderer::writeScreenshot(const std::filesystem::path& path) const {
    const int width = overlayActive_ ? overlayWidth_ : width_;
    const int height = overlayActive_ ? overlayHeight_ : height_;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, overlayActive_ ? overlayFramebuffer_ : framebuffer_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    checkErrors("screenshot readback");
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot write screenshot: " + path.string());
    output << "P6\n" << width << ' ' << height << "\n255\n";
    const auto stride = static_cast<std::size_t>(width) * 3;
    for (int row = height - 1; row >= 0; --row)
        output.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(row) * stride), static_cast<std::streamsize>(stride));
    if (!output) throw std::runtime_error("Error writing screenshot: " + path.string());
    // A deterministic readback confirms a draw produced pixels beyond the sky.
    std::size_t foreground = 0;
    const auto background = glm::clamp(backgroundSrgb_, glm::vec3(0), glm::vec3(1));
    for (std::size_t p = 0; p < pixels.size(); p += 3)
        if (std::abs(static_cast<int>(pixels[p]) - static_cast<int>(std::lround(background.r * 255))) > 2 ||
            std::abs(static_cast<int>(pixels[p + 1]) - static_cast<int>(std::lround(background.g * 255))) > 2 ||
            std::abs(static_cast<int>(pixels[p + 2]) - static_cast<int>(std::lround(background.b * 255))) > 2) ++foreground;
    std::cout << "Screenshot: " << path << " (" << width << 'x' << height
              << ", " << foreground << " foreground pixels)\n";
    if (!foreground) throw std::runtime_error("Render readback contains only background; model may be out of view");
}

} // namespace viewer
