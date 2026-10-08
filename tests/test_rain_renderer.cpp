#include "renderer.hpp"
#include "rain_renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int width = 640, height = 480;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::Camera camera() {
    viewer::Camera view;
    view.setPosition({0, 1.7f, 0});
    view.look(0, 200);
    return view;
}

std::vector<unsigned char> pixels() {
    std::vector<unsigned char> image(width * height * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image.data());
    return image;
}

std::size_t changed(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    std::size_t result = 0;
    for (std::size_t i = 0; i < a.size(); i += 3)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++result;
    return result;
}

double addedBrightness(const std::vector<unsigned char>& rain, const std::vector<unsigned char>& sky,
                       int x0, int x1, int y0, int y1) {
    double result = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto i = static_cast<std::size_t>((y * width + x) * 3);
            for (int c = 0; c < 3; ++c) result += static_cast<int>(rain[i + c]) - sky[i + c];
        }
    return result / ((x1 - x0) * (y1 - y0) * 3);
}

void checkRain() {
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR);
    renderer.setShadows(false);
    renderer.resize(width, height);
    const auto view = camera();
    renderer.render(view);
    const auto dry = pixels();
    renderer.setRain(true);
    renderer.render(view, 0);
    const auto first = pixels();
    require(changed(first, dry) > 300, "Enabled rain must draw visible streaks");
    renderer.render(view, 0.25);
    require(changed(pixels(), first) > 300, "Rain must move as animation time advances");
    renderer.render(view, 0);
    require(pixels() == first, "Frozen animation time must produce repeatable rain");
    renderer.setRain(false);
    renderer.render(view);
    require(pixels() == dry, "Disabling rain must restore the dry scene");

    renderer.setNight(true);
    renderer.render(view);
    const auto nightSky = pixels();
    renderer.setRain(true);
    renderer.render(view);
    const auto nightRain = pixels();
    const auto core = addedBrightness(nightRain, nightSky, 260, 380, 180, 300);
    const auto outside = addedBrightness(nightRain, nightSky, 0, 100, 0, height);
    require(core > outside * 1.5 && core > 0.2,
            "Night rain must brighten inside the camera flashlight cone");
    viewer::Renderer::checkErrors("rain visibility, animation, and night lighting");
}

void checkOcclusion() {
    viewer::Model model;
    viewer::Material material;
    material.unlit = true;
    model.materials.push_back(material);
    viewer::Primitive wall;
    wall.material = 0;
    for (const auto xy : {glm::vec2(-10, -10), glm::vec2(10, -10), glm::vec2(10, 10), glm::vec2(-10, 10)}) {
        viewer::Vertex vertex;
        vertex.position = {xy.x, xy.y + 1.7f, -0.15f};
        vertex.normal = {0, 0, 1};
        vertex.color = {0.1f, 0.1f, 0.1f, 1};
        wall.vertices.push_back(vertex);
    }
    wall.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(wall);
    model.draws.push_back({0, glm::mat4(1), "rain-occluding-wall"});
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setShadows(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    renderer.render(camera());
    const auto dry = pixels();
    renderer.setRain(true);
    renderer.render(camera(), 0.25);
    require(pixels() == dry, "Opaque scene depth must block rain behind it");
    renderer.setRain(false);
    renderer.render(camera());
    require(pixels() == dry, "Rain must leave scene depth and subsequent draws intact");
}

void checkState() {
    viewer::Renderer target(viewer::Model{}, VIEWER_SHADER_DIR);
    target.setShadows(false);
    target.resize(width, height);
    target.render(camera());
    viewer::RainRenderer rain(VIEWER_SHADER_DIR);
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE, GL_ZERO);
    glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
    rain.draw(camera(), static_cast<float>(width) / height, 0, false, viewer::NightLighting{});
    GLint actual = 0;
    GLboolean depthWrite = GL_FALSE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &actual);
    require(actual == program, "Rain must restore the active shader program");
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &actual);
    require(static_cast<GLuint>(actual) == vao, "Rain must restore the active VAO");
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(depthWrite && !glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_BLEND) && glIsEnabled(GL_CULL_FACE),
            "Rain must restore depth, blend, and culling enables");
    const std::array<std::array<GLenum, 2>, 6> expected{{
        {{GL_BLEND_SRC_RGB, GL_ONE_MINUS_SRC_ALPHA}}, {{GL_BLEND_DST_RGB, GL_SRC_ALPHA}},
        {{GL_BLEND_SRC_ALPHA, GL_ONE}}, {{GL_BLEND_DST_ALPHA, GL_ZERO}},
        {{GL_BLEND_EQUATION_RGB, GL_FUNC_REVERSE_SUBTRACT}}, {{GL_BLEND_EQUATION_ALPHA, GL_FUNC_SUBTRACT}}
    }};
    for (const auto value : expected) {
        glGetIntegerv(value[0], &actual);
        require(static_cast<GLenum>(actual) == value[1], "Rain must restore blend factors and equations");
    }
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    viewer::Renderer::checkErrors("rain state restoration");
}
} // namespace

int main() {
    if (!glfwInit()) { std::cout << "SKIP: no display\n"; return 77; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Rain regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        checkRain();
        checkOcclusion();
        checkState();
        std::cout << "Rain rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
