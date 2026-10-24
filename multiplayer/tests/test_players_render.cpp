#include "client/players.hpp"
#include "renderer.hpp"
#include "interior_fixture.hpp"
#include "player_body.hpp"
#include "physics_world.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 640, height = 480;
glm::vec3 pixel() {
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return {rgb[0], rgb[1], rgb[2]};
}
void check(const std::filesystem::path& screenshot) {
    viewer::Model model; model.materials.emplace_back();
    auto& glass = model.materials[0]; glass.alphaMode = "BLEND"; glass.unlit = true;
    glass.doubleSided = true; glass.baseColor = {1, 0, 0, .5f};
    interior_fixture::quad(model, {-2, -1, -2}, {2, -1, -2}, {2, 3, -2}, {-2, 3, -2}, "glass");
    interior_fixture::bounds(model);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height); renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    viewer::Camera camera; camera.setPosition({0, .9f, 0}); camera.setOrientation(-90, 0);
    multiplayer::PlayerRenderer players;
    multiplayer::PlayerState player; player.id = 2; player.movement.feet = {0, 0, -4};
    const auto draw = [&] { players.draw(camera, {player}, 1, width, height); };
    // The old ordering provides the opaque player's actual framebuffer color.
    renderer.render(camera); draw(); const auto uncovered = pixel();
    renderer.render(camera, 0, draw); const auto behindGlass = pixel();
    if (!(behindGlass.r > uncovered.r + 40 && behindGlass.b < uncovered.b - 30))
        throw std::runtime_error("Remote player overwrote the transparent glass tint");
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot);
    player.movement.feet.z = -1;
    renderer.render(camera, 0, draw); const auto inFront = pixel();
    if (glm::length(inFront - uncovered) > 5) throw std::runtime_error("Glass tinted a player in front of it");
    auto physics=std::make_shared<viewer::PhysicsWorld>(); viewer::PlayerBody body(physics,1.7f);
    body.follow({0,1.7f,-4},{0,0,-1},true); body.punch({0,0,-1});
    for (int n=0;n<4;++n) physics->step(1.f/60,[&](float dt){body.beforeStep(dt);},[&](float){body.afterStep();});
    player.slot=static_cast<std::uint8_t>(viewer::punchSlot); player.melee=body.pose();
    renderer.render(camera); draw(); const auto bodyColor=pixel();
    renderer.render(camera,0,draw); const auto bodyBehindGlass=pixel();
    if (!(bodyBehindGlass.r>bodyColor.r+40 && bodyBehindGlass.b<bodyColor.b-30))
        throw std::runtime_error("Remote physical melee body overwrote the glass tint");
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot.string()+"-melee.ppm");
    renderer.render(camera); const auto empty=pixel();
    renderer.render(camera,0,[&]{players.draw(camera,{player},player.id,width,height);});
    if (glm::length(pixel()-empty)>1) throw std::runtime_error("Local player also rendered as a remote melee body");
    GLboolean depthWrite = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    if (!depthWrite) throw std::runtime_error("Opaque player hook leaked depth state");
    viewer::Renderer::checkErrors("multiplayer players and glass");
}
}
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "multiplayer glass regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window); glEnable(GL_DEPTH_TEST);
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        check(argc > 1 ? argv[1] : std::filesystem::path{});
        std::cout << "Remote players behind/in front of glass passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
