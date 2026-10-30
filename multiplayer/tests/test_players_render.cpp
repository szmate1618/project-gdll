#include "client/players.hpp"
#include "renderer.hpp"
#include "interior_fixture.hpp"
#include "player_body.hpp"
#include "physics_world.hpp"
#include "weapon_overlay.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <cmath>
#include <fstream>
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
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(width * height * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, result.data());
    return result;
}
std::size_t differences(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    std::size_t result = 0;
    for (std::size_t i = 0; i < a.size(); i += 3)
        if (a[i] != b[i] || a[i+1] != b[i+1] || a[i+2] != b[i+2]) ++result;
    return result;
}
void worldWeapons(const std::filesystem::path& screenshot) {
    viewer::WeaponOverlay geometry(VIEWER_SHADER_DIR, VIEWER_WEAPON_ASSETS);
    multiplayer::RemoteWeaponPlayback playback;
    multiplayer::PlayerState player; player.id = 2; player.slot = 2;
    player.movement.feet = {0, 0, -3}; player.yaw = 0;
    viewer::Camera camera; camera.setPosition({.25f, 1.5f, 0}); camera.setOrientation(-90, 0);
    const auto clear = [](double depth) {
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
        glClearDepth(depth); glClearColor(.1f, .2f, .3f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    };
    const auto unusualState = [] {
        glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT); glFrontFace(GL_CW); glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE);
        glDepthRange(.2, .8); glEnable(GL_SCISSOR_TEST); glScissor(0, 0, width, height);
        glUseProgram(0); glBindVertexArray(0);
    };
    const auto checkState = [](double expectedClearDepth) {
        GLint program = -1, vao = -1, front = 0, cull = 0, function = 0;
        GLboolean write = GL_TRUE; GLdouble depthRange[2]{}, clearDepth = -1;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_FRONT_FACE, &front); glGetIntegerv(GL_CULL_FACE_MODE, &cull);
        glGetIntegerv(GL_DEPTH_FUNC, &function); glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
        glGetDoublev(GL_DEPTH_RANGE, depthRange); glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        if (glIsEnabled(GL_DEPTH_TEST) || !glIsEnabled(GL_BLEND) || !glIsEnabled(GL_CULL_FACE) ||
            !glIsEnabled(GL_SCISSOR_TEST) || program != 0 || vao != 0 || front != GL_CW || cull != GL_FRONT ||
            function != GL_GREATER || write || std::abs(depthRange[0]-.2) > 1e-6 ||
            std::abs(depthRange[1]-.8) > 1e-6 || clearDepth != expectedClearDepth)
            throw std::runtime_error("World weapon draw leaked GL state");
    };
    for (const float yaw : {0.f, 180.f}) {
        player.yaw = yaw;
        const auto sample = playback.sample(player, 0);
        if (!sample) throw std::runtime_error("World weapon fixture has no equipped gun");
        clear(1); const auto background = pixels(); unusualState();
        geometry.drawWorld(sample->slot, camera, float(width)/height, sample->eyeToWorld);
        checkState(1);
        const auto visible = pixels();
        if (differences(background, visible) < 100)
            throw std::runtime_error("Remote weapon has no visible side silhouette with normal world culling");
        if (!screenshot.empty()) {
            std::ofstream capture(screenshot.string() + (yaw == 0 ? "-gun-right.ppm" : "-gun-left.ppm"), std::ios::binary);
            capture << "P6\n" << width << ' ' << height << "\n255\n";
            for (int y = height - 1; y >= 0; --y)
                capture.write(reinterpret_cast<const char*>(visible.data() + y * width * 3), width * 3);
            if (!capture) throw std::runtime_error("Cannot save remote weapon capture");
        }
        // A foreground wall's depth must survive this scene draw. Clearing
        // depth as in the local overlay would incorrectly reveal the gun.
        clear(0); unusualState();
        geometry.drawWorld(sample->slot, camera, float(width)/height, sample->eyeToWorld);
        checkState(0);
        if (pixels() != background) throw std::runtime_error("Remote weapon cleared or bypassed foreground scene depth");
    }
    glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glCullFace(GL_BACK); glFrontFace(GL_CCW); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDepthRange(0, 1);
    player.yaw = 0;
    multiplayer::PlayerRenderer bodyOnly, equipped(&geometry);
    clear(1); bodyOnly.draw(camera, {player}, 1, width, height); const auto body = pixels();
    clear(1); equipped.draw(camera, {player}, 1, width, height);
    if (differences(body, pixels()) < 100) throw std::runtime_error("Remote player did not draw its borrowed firearm geometry");
    clear(1); const auto background = pixels(); equipped.draw(camera, {player}, player.id, width, height);
    if (pixels() != background) throw std::runtime_error("Local player drew an additional world-space weapon");
    glClearDepth(1);
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
    worldWeapons(screenshot);
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
