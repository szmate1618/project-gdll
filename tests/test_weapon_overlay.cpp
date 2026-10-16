#include "weapon_overlay.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(800, 600, "weapon overlay check", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        viewer::WeaponOverlay overlay(VIEWER_SHADER_DIR, VIEWER_WEAPON_ASSETS);
        glViewport(0, 0, 800, 600);
        glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
        glCullFace(GL_FRONT); glFrontFace(GL_CW);
        glUseProgram(0); glBindVertexArray(0);
        std::vector<unsigned char> pixels(800 * 600 * 3);
        std::set<std::size_t> silhouetteCounts;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            glClearColor(.1f, .2f, .3f, 1);
            // Simulate a wall pressed against the camera. Weapons must use a
            // fresh depth layer while retaining self-occlusion between parts.
            glClearDepth(0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE);
            glDepthRange(.2, .8);
            glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 1, 1);
            overlay.draw(slot, 800, 600);
            GLint program = -1, vao = -1, front = -1, cull = -1, depthFunction = 0;
            GLboolean depthWrite = GL_FALSE;
            GLdouble clearDepth = 1, depthRange[2]{};
            glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
            glGetIntegerv(GL_FRONT_FACE, &front); glGetIntegerv(GL_CULL_FACE_MODE, &cull);
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
            glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth); glGetDoublev(GL_DEPTH_RANGE, depthRange);
            if (!glIsEnabled(GL_DEPTH_TEST) || !glIsEnabled(GL_BLEND) || !glIsEnabled(GL_CULL_FACE) ||
                !glIsEnabled(GL_SCISSOR_TEST) || depthWrite || program != 0 || vao != 0 ||
                front != GL_CW || cull != GL_FRONT || depthFunction != GL_GREATER || clearDepth != 0 ||
                std::abs(depthRange[0] - .2) > 1e-6 || std::abs(depthRange[1] - .8) > 1e-6)
                throw std::runtime_error("Weapon draw must restore GL state");
            glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
            glReadPixels(0, 0, 800, 600, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::size_t foreground = 0;
            std::set<unsigned> colors;
            for (std::size_t i = 0; i < pixels.size(); i += 3)
                if (pixels[i] != pixels[0] || pixels[i+1] != pixels[1] || pixels[i+2] != pixels[2]) {
                    ++foreground;
                    colors.insert((unsigned(pixels[i]) << 16) | (unsigned(pixels[i+1]) << 8) | pixels[i+2]);
                }
            if (foreground < 500 || foreground > 800 * 600 / 4 || colors.size() < 3 ||
                !silhouetteCounts.insert(foreground).second)
                throw std::runtime_error("Each weapon must render a distinct shaded mesh in a corner of the screen");
            if (argc > 1) {
                std::ofstream capture(std::string(argv[1]) + "-" + std::to_string(slot + 1) + ".ppm", std::ios::binary);
                capture << "P6\n800 600\n255\n";
                for (int y = 599; y >= 0; --y)
                    capture.write(reinterpret_cast<const char*>(pixels.data() + y * 800 * 3), 800 * 3);
            }
        }
        viewer::Renderer::checkErrors("weapon overlays");
        std::cout << "All four weapon silhouettes and GL state checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
