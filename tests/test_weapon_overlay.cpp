#include "weapon_overlay.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
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
        viewer::WeaponOverlay overlay(VIEWER_SHADER_DIR);
        glViewport(0, 0, 800, 600);
        glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
        glCullFace(GL_FRONT); glFrontFace(GL_CW);
        glUseProgram(0); glBindVertexArray(0);
        std::vector<unsigned char> pixels(800 * 600 * 3);
        std::size_t previousCount = 0;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            glClearColor(.1f, .2f, .3f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            overlay.draw(slot, 800, 600);
            GLint program = -1, vao = -1, front = -1, cull = -1;
            GLboolean depthWrite = GL_FALSE;
            glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
            glGetIntegerv(GL_FRONT_FACE, &front); glGetIntegerv(GL_CULL_FACE_MODE, &cull);
            if (!glIsEnabled(GL_DEPTH_TEST) || !glIsEnabled(GL_BLEND) || !glIsEnabled(GL_CULL_FACE) ||
                !depthWrite || program != 0 || vao != 0 || front != GL_CW || cull != GL_FRONT)
                throw std::runtime_error("Weapon draw must restore GL state");
            glReadPixels(0, 0, 800, 600, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::size_t greyCount = 0;
            for (std::size_t i = 0; i < pixels.size(); i += 3)
                if (pixels[i] == pixels[i+1] && pixels[i] == pixels[i+2] && pixels[i] > 50) ++greyCount;
            if (greyCount < 500 || greyCount == previousCount)
                throw std::runtime_error("Each weapon must render a visible, distinct grey box silhouette");
            previousCount = greyCount;
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
