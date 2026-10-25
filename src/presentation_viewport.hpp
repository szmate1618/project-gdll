#pragma once

#include <algorithm>
#include <cmath>
#include <glm/vec2.hpp>

namespace viewer {
// Fitted scene rectangle in presentation-framebuffer pixels, with a top-left
// origin. Window fractions also describe framebuffer fractions on HiDPI displays.
struct PresentationViewport {
    int x = 0, y = 0, width = 0, height = 0;
    int targetWidth = 0, targetHeight = 0;

    glm::vec2 scenePointer(float windowX, float windowY, int sceneWidth, int sceneHeight) const {
        // Keep points in the black bars outside the scene, rather than clamping
        // them onto an interactive element at its edge.
        return {(windowX * targetWidth - x) * sceneWidth / width,
                (windowY * targetHeight - y) * sceneHeight / height};
    }
};

inline PresentationViewport presentationViewport(int sceneWidth, int sceneHeight, int targetWidth, int targetHeight) {
    if (sceneWidth <= 0 || sceneHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) return {};
    const double scale = std::min(double(targetWidth) / sceneWidth, double(targetHeight) / sceneHeight);
    const int width = std::max(1, int(std::lround(sceneWidth * scale)));
    const int height = std::max(1, int(std::lround(sceneHeight * scale)));
    const int bottom = (targetHeight - height) / 2;
    return {(targetWidth - width) / 2, targetHeight - bottom - height, width, height, targetWidth, targetHeight};
}
} // namespace viewer
