#include "house_fixture.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"
#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 960, height = 720;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::array<unsigned char, 3> pixel(const viewer::Camera& camera, glm::vec3 point) {
    const auto p = camera.projection(float(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(p) / p.w;
    require(std::abs(ndc.x) < .95f && std::abs(ndc.y) < .95f, "Floorplan sample must be on screen");
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(int((ndc.x + 1) * width * .5f), int((ndc.y + 1) * height * .5f), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return rgb;
}
float depth(const viewer::Camera& camera, glm::vec3 point) {
    const auto p = camera.projection(float(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(p) / p.w;
    float result = 0;
    glReadPixels(int((ndc.x + 1) * width * .5f), int((ndc.y + 1) * height * .5f), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &result);
    return result;
}
void render(const std::filesystem::path& capture) {
    auto model = house_fixture::house();
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer layer(model, doors, collision);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height); renderer.setHaze(false); renderer.setGroundFog(false); renderer.setShadows(false);
    viewer::Camera camera;
    camera.setPosition({2, 1.65f, 5.5f}); camera.setOrientation(-90, 0);
    renderer.render(camera);
    const auto bare = pixel(camera, {2, 1.65f, 4});
    doors.setAngle(1, .9f); layer.update({camera.position()});
    renderer.updateDoors(doors); renderer.updateFloorplans(layer); renderer.render(camera);
    const auto partition = pixel(camera, {2, 1.65f, 4});
    require(partition != bare, "A generated partition must replace the distant bare shell in the camera view");
    camera.setPosition({0, 1.65f, 5.6f});
    renderer.render(camera);
    const auto closed = depth(camera, {.35f, 1.4f, 4});
    doors.setAngle(3, -1.5f);
    renderer.updateFloorplans(layer); renderer.render(camera);
    require(depth(camera, {.35f, 1.4f, 4}) > closed + .00001f, "The interactive interior panel must visibly uncover its doorway");
    // Camera sees the entry partition, the swung panel and the living space.
    camera.setPosition({1.1f, 1.65f, 5.8f}); camera.setOrientation(-122, -5);
    renderer.setShadows(true); renderer.updateDoors(doors); renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture);
    renderer.setNight(true); renderer.setFlashlight(true); renderer.setFlashlightShadows(true); renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".night.ppm");
    layer.update({glm::vec3(100)}); renderer.updateFloorplans(layer); renderer.render(camera);
    GLboolean depthWrite = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(depthWrite == GL_TRUE, "Streamed interiors must retain opaque depth writes");
    viewer::Renderer::checkErrors("floorplan camera, shadow, door movement and eviction");
}
} // namespace
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "floorplan regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        render(argc > 1 ? argv[1] : std::filesystem::path{});
        std::cout << "Floorplan rendering tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
