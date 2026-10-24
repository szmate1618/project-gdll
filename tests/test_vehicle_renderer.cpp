#include "renderer.hpp"
#include "vehicle_layer.hpp"
#include "physics_world.hpp"
#include "vehicle_player.hpp"
#include "interior_fixture.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 960, height = 640;
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
glm::vec3 pixel(const viewer::Camera& camera, glm::vec3 p) {
    const auto clip = camera.projection(float(width) / height) * camera.view() * glm::vec4(p, 1);
    const auto ndc = glm::vec3(clip) / clip.w;
    require(clip.w > 0 && std::abs(ndc.x) < .95f && std::abs(ndc.y) < .95f, "Vehicle sample must be in frame");
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(int((ndc.x + 1) * width / 2), int((ndc.y + 1) * height / 2), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return {rgb[0], rgb[1], rgb[2]};
}
void run() {
    viewer::Model model; model.materials.push_back({});
    interior_fixture::quad(model, {-50, 0, -50}, {-50, 0, 50}, {50, 0, 50}, {50, 0, -50}, "terrain");
    interior_fixture::bounds(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(physics); cars.add({0, .8f, 0});
    viewer::Camera camera; camera.setPosition({5, 3.5f, 6}); camera.setOrientation(-130, -20);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.setShadows(false); renderer.render(camera);
    const auto before = pixel(camera, {.7f, .8f, 1.7f});
    renderer.updateVehicles(cars); renderer.render(camera);
    const auto body = pixel(camera, {.7f, .8f, 1.7f});
    const auto wheel = pixel(camera, {.96f, .48f, 1.15f});
    require(glm::length(body - before) > 30 && body.b > body.r + 15, "Chassis must appear as a blue box");
    require(glm::length(wheel) < glm::length(body) * .6f, "Visible tires must be dark solids beside the chassis");
    renderer.setShadows(true); renderer.render(camera);
    renderer.writeScreenshot("vehicle-day.ppm");
    viewer::CollisionWorld world(model);
    world.setVehicles(&cars);
    viewer::CameraRig rig(world, model.boundsMin, model.boundsMax, float(width) / height);
    require(rig.enterWalking(glm::vec3(-1.8f, 1.7f, -.25f)), "Chase view fixture must start beside car");
    viewer::VehiclePlayer driver;
    require(driver.toggle(cars, rig, world) && driver.toggleThirdPerson(cars, rig.camera(), world),
            "Chase view fixture must occupy the car in third person");
    renderer.updateVehicles(cars, driver.car()); renderer.render(rig.camera());
    const auto chaseBody = pixel(rig.camera(), {0, .8f, 1.8f});
    require(chaseBody.b > chaseBody.r + 15, "Third person must frame the visible car body");
    renderer.writeScreenshot("vehicle-third-person.ppm");
    viewer::Renderer empty(model, VIEWER_SHADER_DIR);
    empty.resize(width, height); empty.setNight(true); empty.setShadows(false); empty.setHaze(false); empty.setGroundFog(false);
    // Keep the camera off to one side so its held flashlight cannot explain the
    // light on the road ahead of the car. Occupied mode turns that light off.
    camera.setPosition({8, 5, -5}); camera.setOrientation(-170, -25);
    empty.render(camera); const auto dark = pixel(camera, {0, 0, -12});
    renderer.setNight(true); renderer.updateVehicles(cars, 0); renderer.render(camera);
    const auto beam = pixel(camera, {0, 0, -12});
    std::cout << "night dark=" << glm::length(dark) << " beam=" << glm::length(beam) << '\n';
    renderer.writeScreenshot("vehicle-night.ppm");
    require(glm::length(beam) > glm::length(dark) + 30, "Downward headlights must illuminate the road ahead independently of camera aim");
    // Move only the configured emitters. The road beams must move with them,
    // independently of the unchanged chassis geometry and the camera flashlight.
    viewer::VehicleConfig shifted;
    for (auto& light : shifted.headlights) light.x += 6;
    viewer::VehicleLayer shiftedCars(physics, {shifted}); shiftedCars.add({0,.8f,0});
    camera.setPosition({-8,6,4}); camera.setOrientation(-55,-18);
    renderer.updateVehicles(cars,0); renderer.render(camera);
    const auto originalRoad = pixel(camera,{0,0,-12});
    const auto originalSide = pixel(camera,{6,0,-12});
    renderer.updateVehicles(shiftedCars,0); renderer.render(camera);
    const auto shiftedRoad = pixel(camera,{0,0,-12});
    const auto shiftedSide = pixel(camera,{6,0,-12});
    require(glm::length(originalRoad) > glm::length(shiftedRoad) + 20 &&
            glm::length(shiftedSide) > glm::length(originalSide) + 20,
            "Headlight beams ignored their configured local positions");
    renderer.writeScreenshot("vehicle-configured-lights.ppm");
    renderer.setFlashlightShadows(true); renderer.render(camera);
    GLboolean depth = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth);
    require(depth == GL_TRUE, "Vehicle rendering must restore opaque depth writes");
    viewer::Renderer::checkErrors("vehicle box, tires, lights and shadow passes");
}
}
int main() {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Vehicle rendering", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window); viewer::initializeOpenGL();
    int result = 0; try { run(); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
