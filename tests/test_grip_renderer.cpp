#include "grip_renderer.hpp"
#include "scene_depth.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int size = 256;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> data(size * size * 3);
    glReadPixels(0, 0, size, size, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    return data;
}
}

int main() {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(size, size, "Grip renderer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    try {
        viewer::initializeOpenGL();
        viewer::GripRenderer renderer(std::filesystem::path(VIEWER_ITEM_ROOT) / "shaders",
                                      viewer::loadModel(VIEWER_GRIP_MODEL));
        viewer::Camera camera;
        camera.setPosition({0, .015f, .4f}); camera.setOrientation(-90, 0);
        glViewport(0, 0, size, size);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        const std::vector<viewer::GripPreviewPose> poses{{}};
        const auto verify=[&](bool reversed) {
            viewer::SceneDepthScope depth(viewer::sceneClipControl(), reversed);
            const double clearDepth = reversed ? 0 : 1;
            glDepthMask(GL_TRUE); glClearDepth(clearDepth);
            glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.draw(camera, 1, poses);
            const auto black=pixels();
            glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.draw(camera, 1, poses);
            const auto white=pixels();
            std::size_t translucent=0;
            for (std::size_t i=0; i<black.size(); i+=3) {
                if (black[i+1]>black[i]+10 &&
                    std::abs(int(white[i])-int(black[i])-127)<3 &&
                    std::abs(int(white[i+1])-int(black[i+1])-127)<3) ++translucent;
            }
            require(translucent>200, "Grip hand must render green at 50 percent opacity");
            std::vector<float> depths(size * size);
            glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());
            for (const auto value : depths)
                require(std::abs(value-clearDepth)<1e-6, "Grip ghost wrote to scene depth");

            glClearDepth(reversed ? 1 : 0);
            glClearColor(.2f, .2f, .2f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const auto occluded=pixels();
            renderer.draw(camera, 1, poses);
            require(pixels()==occluded, "Grip ghost rendered through an occluding surface");

            glDisable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_ALWAYS);
            glDisable(GL_CULL_FACE); glCullFace(GL_FRONT); glFrontFace(GL_CW);
            glDisable(GL_BLEND); glBlendFuncSeparate(GL_DST_COLOR, GL_SRC_COLOR, GL_DST_ALPHA, GL_SRC_ALPHA);
            glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
            renderer.draw(camera, 1, poses);
            GLint value=0; GLboolean flag=GL_FALSE;
            require(!glIsEnabled(GL_DEPTH_TEST) && !glIsEnabled(GL_CULL_FACE) && !glIsEnabled(GL_BLEND),
                    "Grip renderer changed enable states");
            glGetBooleanv(GL_DEPTH_WRITEMASK, &flag); require(flag, "Grip renderer changed depth write state");
            glGetIntegerv(GL_DEPTH_FUNC, &value); require(value==GL_ALWAYS, "Grip renderer changed depth comparison");
            glGetIntegerv(GL_CULL_FACE_MODE, &value); require(value==GL_FRONT, "Grip renderer changed cull mode");
            glGetIntegerv(GL_FRONT_FACE, &value); require(value==GL_CW, "Grip renderer changed winding");
            glGetIntegerv(GL_BLEND_SRC_RGB, &value); require(value==GL_DST_COLOR, "Grip renderer changed RGB blend source");
            glGetIntegerv(GL_BLEND_DST_RGB, &value); require(value==GL_SRC_COLOR, "Grip renderer changed RGB blend destination");
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &value); require(value==GL_DST_ALPHA, "Grip renderer changed alpha blend source");
            glGetIntegerv(GL_BLEND_DST_ALPHA, &value); require(value==GL_SRC_ALPHA, "Grip renderer changed alpha blend destination");
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &value); require(value==GL_FUNC_REVERSE_SUBTRACT, "Grip renderer changed RGB blend equation");
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value); require(value==GL_FUNC_SUBTRACT, "Grip renderer changed alpha blend equation");
        };
        verify(false);
        if (viewer::sceneClipControl()) verify(true);
        require(glGetError()==GL_NO_ERROR, "Grip renderer generated a GL error");
        std::cout << "Grip rendering checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    glfwDestroyWindow(window); glfwTerminate(); return 0;
}
