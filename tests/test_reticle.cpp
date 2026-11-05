#include "reticle_overlay.hpp"
#include "weapon_accuracy.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<unsigned char> render(viewer::ReticleOverlay& reticle, viewer::Camera::ViewMode mode,
                                  int width, int height, float radius) {
    glViewport(0, 0, width, height);
    glClearColor(.08f, .10f, .12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
    reticle.draw(mode, width, height, radius);
    require(!glIsEnabled(GL_DEPTH_TEST) && !glIsEnabled(GL_BLEND) && glIsEnabled(GL_CULL_FACE),
            "Reticle expansion must preserve caller GL state");
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "Reticle rendering produced a GL error");
    return pixels;
}
int gap(const std::vector<unsigned char>& pixels, int width, int height, bool horizontal) {
    for (int offset = 0; offset < height / 2; ++offset) {
        const int x = width / 2 + (horizontal ? offset : 0);
        const int y = height / 2 + (horizontal ? 0 : offset);
        if (pixels[(static_cast<std::size_t>(y) * width + x) * 3] > 110) return offset;
    }
    throw std::runtime_error("Expanded crosshair disappeared from the framebuffer");
}
void save(const std::filesystem::path& path, const std::vector<unsigned char>& pixels, int width, int height) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y)
        out.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(y) * width * 3), width * 3);
    require(bool(out), "Could not save crosshair capture");
}
} // namespace
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(1280, 720, "reticle check", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        viewer::ReticleOverlay reticle(VIEWER_SHADER_DIR);
        require(argc == 2, "Expected capture directory");
        std::filesystem::create_directories(argv[1]);
        for (const auto size : {glm::ivec2(640, 480), glm::ivec2(1280, 720)}) {
            int previous = -1;
            for (const float sigma : {0.f, .5f, 1.f, 2.f}) {
                const float radius = viewer::inaccuracyCrosshairRadius(sigma, 1.7320508f);
                const auto pixels = render(reticle, viewer::Camera::ViewMode::original, size.x, size.y, radius);
                const int horizontal = gap(pixels, size.x, size.y, true), vertical = gap(pixels, size.x, size.y, false);
                require(horizontal > previous && std::abs(horizontal - vertical) <= 2,
                        "Crosshair gap must expand monotonically and equally on both axes at different aspects");
                previous = horizontal;
                save(std::filesystem::path(argv[1]) / (std::to_string(size.x) + "-sigma-" + std::to_string(sigma) + ".ppm"),
                     pixels, size.x, size.y);
            }
            for (const auto mode : {viewer::Camera::ViewMode::zoom10, viewer::Camera::ViewMode::zoom1})
                require(render(reticle, mode, size.x, size.y, 0) == render(reticle, mode, size.x, size.y, .3f),
                        "Hip-fire expansion must not change the scoped reticle");
        }
        std::cout << "Crosshair expansion, aspect ratios, scopes and GL state passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
