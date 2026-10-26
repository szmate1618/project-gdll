#include "renderer.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Pixel = std::array<unsigned char, 3>;
constexpr Pixel red{255, 0, 0}, green{0, 255, 0}, black{0, 0, 0};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Pixel pixel(int x, int y) {
    Pixel result{};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, result.data());
    return result;
}

void checkCapture(const viewer::Renderer& renderer, int width, int height, bool overlay) {
    const auto path = std::filesystem::current_path() / ("presentation-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ppm");
    renderer.writeScreenshot(path);
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    int actualWidth = 0, actualHeight = 0, maximum = 0;
    file >> magic >> actualWidth >> actualHeight >> maximum;
    require(magic == "P6" && actualWidth == width && actualHeight == height && maximum == 255,
        "Capture must use the current scene or composed GUI dimensions");
    file.get();
    if (overlay) {
        // The one-pixel GUI marker is in a black bar, outside the scaled scene.
        file.seekg((static_cast<std::streamoff>(height - 1 - 10) * width + 17) * 3, std::ios::cur);
        Pixel marker{};
        file.read(reinterpret_cast<char*>(marker.data()), marker.size());
        require(marker == green, "Capture must retain GUI pixels at native resolution");
    }
    file.close();
    std::filesystem::remove(path);
}

void checkPresentation() {
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR);
    renderer.setShadows(false);
    renderer.resize(80, 40);
    const auto scene = [&] {
        renderer.render(viewer::Camera{});
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    };
    scene();
    GLint sceneFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &sceneFramebuffer);

    const auto viewport = renderer.beginOverlay(320, 240);
    require(viewport.x == 0 && viewport.y == 40 && viewport.width == 320 && viewport.height == 160,
        "Reticle viewport must follow the fitted scene, independently of the full-window GUI");
    require(pixel(0, 0) == black && pixel(160, 120) == red && pixel(0, 239) == black,
        "Composed scene must preserve its color and black presentation bars");

    // A one-pixel overlay must survive presentation without enlargement or blur.
    glViewport(0, 0, 320, 240);
    glEnable(GL_SCISSOR_TEST);
    glScissor(17, 10, 1, 1);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    renderer.present(320, 240);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    require(pixel(17, 10) == green && pixel(16, 10) == black && pixel(18, 10) == black,
        "GUI pixels must be presented one-to-one at window resolution");
    checkCapture(renderer, 320, 240, true);

    // A new scene must release the previous composed image even at unchanged size.
    scene();
    checkCapture(renderer, 80, 40, false);
    renderer.beginOverlay(192, 108);
    require(pixel(96, 54) == red && pixel(0, 0) == black && pixel(0, 107) == black,
        "Resizing the GUI target must recreate its fitted scene and bars");
    checkCapture(renderer, 192, 108, false);

    scene();
    renderer.beginOverlay(80, 40);
    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    require(framebuffer == sceneFramebuffer && pixel(17, 10) == red,
        "Matching resolutions must reuse the scene target without stale overlay pixels");
    checkCapture(renderer, 80, 40, false);
    viewer::Renderer::checkErrors("scene and native-resolution GUI composition");
}
} // namespace

int main() {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(320, 240, "Presentation regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    viewer::initializeOpenGL();
    int result = 0;
    try {
        checkPresentation();
        std::cout << "Native-resolution GUI composition tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
