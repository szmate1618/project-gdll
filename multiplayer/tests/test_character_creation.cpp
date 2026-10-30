#include "client/character_creation.hpp"
#include "renderer.hpp"
#include "resources.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 640, height = 480;
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> image(width * height * 3);
    glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,image.data());
    return image;
}
void save(const std::filesystem::path& path, const std::vector<unsigned char>& image) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height-1; y >= 0; --y)
        file.write(reinterpret_cast<const char*>(image.data() + y * width * 3), width * 3);
    if (!file) throw std::runtime_error("Cannot save character preview capture");
}
void check(const std::filesystem::path& capture) {
    const auto profile = std::filesystem::path("character-creation-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    multiplayer::CharacterCreation creation(profile);
    const auto draw = [&](glm::vec2 pointer, bool paint = false, bool orbit = false) {
        glClearColor(.045f,.065f,.085f,1); glClearDepth(1); glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        creation.draw({0,0,width,height}, pointer, paint, orbit);
    };
    draw({-1,-1}); const auto before = pixels();
    if (!capture.empty()) save(capture.string() + "-before.ppm", before);
    draw({.5f,.55f}, true);
    draw({-1,-1}); const auto painted = pixels();
    std::size_t changed = 0;
    for (std::size_t i = 0; i < before.size(); i += 3)
        if (before[i] != painted[i] || before[i+1] != painted[i+1] || before[i+2] != painted[i+2]) ++changed;
    if (changed < 30) throw std::runtime_error("Camera-through-cursor painting did not change the rendered box");
    const auto cursorPixel = (static_cast<std::size_t>(int(height * .45f)) * width + width/2) * 3;
    if (std::equal(before.begin() + cursorPixel, before.begin() + cursorPixel + 3, painted.begin() + cursorPixel))
        throw std::runtime_error("Paint appeared away from the cursor's visible surface");
    draw({-1,-1}, true);
    if (pixels() != painted) throw std::runtime_error("Painting outside the preview changed the box");
    if (!capture.empty()) save(capture.string() + "-painted.ppm", painted);
    if (!creation.save()) throw std::runtime_error("Painted character failed to save");
    {
        multiplayer::CharacterCreation restored(profile);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        restored.draw({0,0,width,height}, {-1,-1}, false, false);
        if (pixels() != painted) throw std::runtime_error("Saved character did not restore the rendered paint and icon");
    }
    std::filesystem::remove(profile);
    draw({.5f,.5f}, false, true); draw({.8f,.5f}, false, true);
    draw({-1,-1});
    if (pixels() == painted) throw std::runtime_error("Right drag failed to rotate the character");
    if (!capture.empty()) save(capture.string() + "-rotated.ppm", pixels());
    viewer::Renderer::checkErrors("Character creation preview and painting");
    std::cout << "Character preview, random face (" << creation.faceName() << "), mouse painting and orbit passed\n";
}
}
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width,height,"Character creation regression",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        check(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
