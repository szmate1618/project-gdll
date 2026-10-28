#include "interior_fixture.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "interior_objects.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include "collision_world.hpp"
#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 800, height = 600;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
glm::vec3 pixel(const viewer::Camera& camera, glm::vec3 point) {
    const auto clip = camera.projection(static_cast<float>(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(clip) / clip.w;
    require(std::abs(ndc.x) < .9f && std::abs(ndc.y) < .9f, "Box sample must be visible");
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(static_cast<int>((ndc.x + 1) * width * .5f), static_cast<int>((ndc.y + 1) * height * .5f),
                 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return {rgb[0], rgb[1], rgb[2]};
}
void dynamicRendering() {
    viewer::Model model;
    model.materials.push_back({});
    interior_fixture::quad(model, {-20, 0, -20}, {-20, 0, 20}, {20, 0, 20}, {20, 0, -20}, "terrain");
    interior_fixture::bounds(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    viewer::CollisionWorld world(model);
    viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
    scene.ensureNear({-10, 0, -10}, {10, 2, 10});
    viewer::Camera camera;
    camera.setPosition({0, 1, 0}); camera.look(0, 200);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.render(camera);
    const auto background = pixel(camera, {0, .5f, -5});
    boxes.add({0, .52f, -5}, {1, 1, 1});
    boxes.add({100, .52f, -5}, {1, 1, 1}); // Must be rejected by the prop BVH.
    renderer.updateBoxes(boxes); renderer.render(camera);
    const auto boxPixel = pixel(camera, {0, .5f, -5});
    require(glm::length(boxPixel - background) > 40 && boxPixel.r > boxPixel.b + 20, "New untextured box must render as a lit brown solid");
    // Explicit walking push at the front-right face; current physics poses
    // must reach the GPU after the body moves and rotates.
    for (int i = 0; i < 45; ++i) {
        const auto p = boxes.pose(0);
        boxes.push(p.center + glm::vec3(.7f, -.5f, 0), .3f, 1.8f, {-3, 0, 0}, 1.0f / 60);
        scene.prepare(boxes); physics->step(1.0f / 60);
    }
    const auto moved = boxes.pose(0);
    require(moved.center.x < -1, "Rendering fixture box must physically move");
    renderer.updateBoxes(boxes); renderer.render(camera);
    require(glm::length(pixel(camera, {0, .5f, -5}) - background) < 5, "Moved box must uncover its old pixels");
    require(glm::length(pixel(camera, moved.center) - background) > 40, "Moved box must appear at its current body pose");
    boxes.retire(0);
    renderer.updateBoxes(boxes); renderer.render(camera);
    require(glm::length(pixel(camera, moved.center) - background) < 5,
            "A retired slot must disappear from rendering even though its last pose is retained");
    renderer.setShadows(true); renderer.render(camera);
    renderer.setNight(true); renderer.setFlashlightShadows(true); renderer.render(camera);
    GLboolean depthWrite = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(depthWrite == GL_TRUE, "Box rendering must preserve depth writes");
    viewer::Renderer::checkErrors("dynamic box and shadow rendering");
}
void captureRoom(const std::filesystem::path& capture) {
    auto model = interior_fixture::town(true);
    model.materials.front().doubleSided = true;
    model.materials.front().baseColor = {.45f, .48f, .52f, 1};
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::BoxLayer boxes(physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
    viewer::InteriorObjects interiors(model, doors, 14);
    require(interiors.populate(0, world, scene, boxes) >= 12, "Visual room fixture must contain a populated interior");
    for (int i = 0; i < 90; ++i) { scene.prepare(boxes); physics->step(1.0f / 60); }
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    viewer::Camera camera;
    camera.setPosition({6, 2.7f, 5.5f});
    camera.look(-450, 50); // Look diagonally across the room, slightly down.
    renderer.resize(width, height); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.updateBoxes(boxes); renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture);
    renderer.setNight(true); renderer.setFlashlightShadows(true); renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".night.ppm");
    viewer::Renderer::checkErrors("populated room rendering");
}
} // namespace
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "box regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        dynamicRendering(); captureRoom(argc > 1 ? argv[1] : std::filesystem::path{});
        std::cout << "Box rendering tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
