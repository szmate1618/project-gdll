#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 640, height = 480;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void card(viewer::Model& model, glm::vec3 center, glm::vec2 size, int material, float detail = 0) {
    viewer::Primitive primitive;
    primitive.material = material;
    for (const auto corner : std::array<glm::vec2, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) {
        viewer::Vertex vertex;
        vertex.position = center + glm::vec3(corner * size * .5f, 0);
        vertex.normal = {0, 0, 1};
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), "facade", detail});
    model.primitives.push_back(primitive);
}

glm::vec3 pixel(const viewer::Camera& camera, glm::vec3 point) {
    const auto clip = camera.projection(static_cast<float>(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(clip) / clip.w;
    require(std::abs(ndc.x) < .9f && std::abs(ndc.y) < .9f, "Facade sample must be inside the camera frustum");
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(static_cast<int>((ndc.x + 1) * width * .5f),
                 static_cast<int>((ndc.y + 1) * height * .5f), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return {rgb[0], rgb[1], rgb[2]};
}

void check(float distance, int zooms, bool detailVisible, bool culling) {
    viewer::Model model;
    viewer::Material wall, door, window;
    wall.unlit = door.unlit = window.unlit = true; // Isolate alpha/LoD from lighting.
    door.baseColor = {112.0f / 255, 65.0f / 255, 32.0f / 255, 1};
    window.baseColor = {86.0f / 255, 156.0f / 255, 210.0f / 255, .4f};
    window.alphaMode = "BLEND";
    model.materials = {wall, door, window};
    card(model, {0, 3, -distance}, {10, 6}, 0);
    card(model, {2, 3, -distance + .025f}, {1, 2.1f}, 1, 120);
    card(model, {-2, 3, -distance + .025f}, {1.2f, 1.2f}, 2, 120);
    // Outside both scope frustums, but within the scope's detail range.
    card(model, {100, 3, -distance}, {1, 2}, 1, 120);

    viewer::Camera camera;
    camera.setPosition({0, 3, 0});
    camera.look(0, 200); // Default pitch is -20°, normal mode uses .1°/pixel.
    for (int i = 0; i < zooms; ++i) camera.cycleViewMode();
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setShadows(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.setSceneCulling(culling);
    renderer.resize(width, height);
    renderer.render(camera);
    const auto doorPixel = pixel(camera, {2, 3, -distance + .025f});
    const auto windowPixel = pixel(camera, {-2, 3, -distance + .025f});
    // glTF color factors are linear; the shader encodes RGB as sRGB before the
    // existing framebuffer alpha blend. These are independently rounded values.
    const auto expectedDoor = detailVisible ? glm::vec3(177, 138, 99) : glm::vec3(255);
    const auto expectedWindow = detailVisible ? glm::vec3(216, 235, 247) : glm::vec3(255);
    if (glm::length(doorPixel - expectedDoor) >= 3 || glm::length(windowPixel - expectedWindow) >= 3)
        std::cerr << "distance=" << distance << " zoom=" << zooms << " culling=" << culling
                  << " door=" << doorPixel.r << ',' << doorPixel.g << ',' << doorPixel.b
                  << " window=" << windowPixel.r << ',' << windowPixel.g << ',' << windowPixel.b << '\n';
    require(glm::length(doorPixel - expectedDoor) < 3, "Brown door must follow near/scope LoD selection");
    require(glm::length(windowPixel - expectedWindow) < 3, "Window must blend at 40% opacity and follow LoD selection");
    if (culling) require(renderer.sceneStats().visible == (detailVisible ? 3u : 1u),
                         "Scope frustum must still reject the off-screen door");
    GLboolean depthWrite = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(depthWrite == GL_TRUE, "Window pass must restore depth writes");
    viewer::Renderer::checkErrors("building facade regression");
}

void checkOpenings(float distance, int zooms, bool near, bool culling) {
    viewer::Model model;
    viewer::Material wall, door, glass, background;
    wall.unlit = door.unlit = glass.unlit = background.unlit = true;
    door.baseColor = {112.0f / 255, 65.0f / 255, 32.0f / 255, 1};
    glass.baseColor = {86.0f / 255, 156.0f / 255, 210.0f / 255, .4f};
    glass.alphaMode = "BLEND";
    background.baseColor = {0, 1, 0, 1};
    model.materials = {wall, door, glass, background};
    card(model, {0, 3, -distance}, {10, 6}, 0, 120);
    model.draws.back().lowDetail = true;
    // Masonry around two rectangular openings, at exactly the panel plane.
    card(model, {-3.8f, 3, -distance}, {2.4f, 6}, 0, 120);
    card(model, {.05f, 3, -distance}, {2.9f, 6}, 0, 120);
    card(model, {3.75f, 3, -distance}, {2.5f, 6}, 0, 120);
    card(model, {-2, 4.8f, -distance}, {1.2f, 2.4f}, 0, 120);
    card(model, {-2, 1.2f, -distance}, {1.2f, 2.4f}, 0, 120);
    card(model, {2, 5.025f, -distance}, {1, 1.95f}, 0, 120);
    card(model, {2, .975f, -distance}, {1, 1.95f}, 0, 120);
    card(model, {2, 3, -distance}, {1, 2.1f}, 1, 120);
    card(model, {-2, 3, -distance}, {1.2f, 1.2f}, 2, 120);
    for (auto& draw : model.draws) draw.detailGroup = "cutout-building";
    // The green surface must be visible through the real glass opening, never
    // through the opaque door, and hidden by the original wall at lower LoD.
    card(model, {0, 3, -distance - 2}, {10, 6}, 3);
    viewer::Camera camera;
    camera.setPosition({0, 3, 0});
    camera.look(0, 200);
    for (int i = 0; i < zooms; ++i) camera.cycleViewMode();
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setShadows(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.setSceneCulling(culling);
    renderer.resize(width, height);
    renderer.render(camera);
    const auto expectedGlass = near ? glm::vec3(63, 235, 94) : glm::vec3(255);
    const auto expectedDoor = near ? glm::vec3(177, 138, 99) : glm::vec3(255);
    require(glm::length(pixel(camera, {-2, 3, -distance}) - expectedGlass) < 3,
            "Glass must reveal the surface behind its opening, with no low-detail wall behind it");
    require(glm::length(pixel(camera, {2, 3, -distance}) - expectedDoor) < 3,
            "Flush door must replace masonry in its opening and stay opaque");
    require(renderer.sceneStats().visible == (near ? 10u : 2u),
            "Exactly one wall alternative must render, with or without frustum culling");
    viewer::Renderer::checkErrors("cutout building regression");
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "facade regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    glEnable(GL_DEPTH_TEST);
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        for (const bool culling : {true, false}) {
            check(20, 0, true, culling);
            check(1000, 0, false, culling);
            check(500, 1, true, culling);
            check(750, 1, true, culling);
            check(4000, 2, true, culling);
            check(7500, 2, true, culling);
            checkOpenings(20, 0, true, culling);
            checkOpenings(1000, 0, false, culling);
            checkOpenings(500, 1, true, culling);
            checkOpenings(7500, 2, true, culling);
        }
        // Optional generated fixture and capture path for visual QA, using a
        // deliberate front-facing view instead of the viewer's overview camera.
        if (argc == 3) {
            const auto model = viewer::loadModel(argv[1]);
            viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
            renderer.setShadows(false);
            renderer.setHaze(false);
            renderer.setGroundFog(false);
            renderer.resize(width, height);
            viewer::Camera camera;
            camera.setPosition({6, 3, 18});
            camera.look(0, 200);
            renderer.render(camera);
            renderer.writeScreenshot(argv[2]);
            camera.setPosition({6, 3, 500});
            camera.cycleViewMode();
            camera.cycleViewMode();
            renderer.render(camera);
            renderer.writeScreenshot(std::string(argv[2]) + ".scope.ppm");
            viewer::Renderer::checkErrors("generated facade fixture");
        }
        std::cout << "Building renderer tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
