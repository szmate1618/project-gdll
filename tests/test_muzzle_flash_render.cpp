#include "renderer.hpp"
#include "weapon_overlay.hpp"
#include "weapons.hpp"

#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int width = 800, height = 600;
using Pixels = std::vector<unsigned char>;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::Model room() {
    viewer::Model model;
    viewer::Material material;
    material.baseColor = {.3f, .3f, .3f, 1};
    model.materials.push_back(material);
    viewer::Primitive mesh;
    mesh.material = 0;
    const auto quad = [&](std::array<glm::vec3, 4> corners, glm::vec3 normal) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto p : corners) {
            viewer::Vertex vertex;
            vertex.position = p; vertex.normal = normal;
            mesh.vertices.push_back(vertex);
        }
        for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(first + index);
    };
    quad({{{-4, -1.7f, -3}, {4, -1.7f, -3}, {4, 2.5f, -3}, {-4, 2.5f, -3}}}, {0, 0, 1});
    quad({{{-4, -1.7f, 1}, {4, -1.7f, 1}, {4, -1.7f, -3}, {-4, -1.7f, -3}}}, {0, 1, 0});
    model.primitives.push_back(std::move(mesh));
    model.draws.push_back({0, glm::mat4(1), "room"});
    model.boundsMin = {-4, -1.7f, -3}; model.boundsMax = {4, 2.5f, 1};
    return model;
}

Pixels read() {
    Pixels pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

void localBloodMist(const std::filesystem::path& captures) {
    const auto model = room();
    viewer::ZombieLayer replicated;
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR, nullptr, &replicated);
    renderer.resize(width, height);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.setFlashlight(false);
    viewer::Camera camera;
    camera.setPosition({0, 0, 0}); camera.setOrientation(-90, 0);
    // A stalled server clock deliberately has a different origin from the
    // local effect clock. Every capture renders the same server animation time.
    constexpr double serverSeconds = 120, localStart = 10;
    const auto render = [&](const char* phase) {
        renderer.render(camera, serverSeconds);
        if (!captures.empty()) renderer.writeScreenshot(captures / (std::string("local-mist-") + phase + ".ppm"));
        return read();
    };
    const auto empty = render("off");
    const auto tinted = [&](const Pixels& before, const Pixels& after, int left, int right) {
        std::size_t count = 0;
        for (int y = height / 3; y < height * 2 / 3; ++y)
            for (int x = left; x < right; ++x) {
                const auto pixel = (static_cast<std::size_t>(y) * width + x) * 3;
                const int oldTint = int(before[pixel]) - int(before[pixel + 1]);
                const int newTint = int(after[pixel]) - int(after[pixel + 1]);
                count += newTint > oldTint + 3 && after[pixel + 1] < before[pixel + 1];
            }
        return count;
    };
    viewer::BloodMist local;
    local.emit({0, 0, -1.5f}, viewer::BloodMistSize::large, localStart);
    renderer.setLocalBloodMist(local.sample(localStart + .1));
    const auto predicted = render("predicted");
    require(tinted(empty, predicted, width / 3, width * 2 / 3) > 100,
        "Predicted hit mist must visibly tint the aimed wall before any authoritative effect arrives");

    replicated.setReplicatedBloodMist({{1, {.85f, 0, -1.5f}, .1f, viewer::BloodMistSize::small}}, serverSeconds);
    renderer.updateZombies(replicated);
    const auto combined = render("remote");
    require(tinted(predicted, combined, width * 2 / 3, width * 9 / 10) > 100,
        "Remote authoritative hit mist must remain visible alongside locally predicted mist");
    replicated.setReplicatedBloodMist({}, serverSeconds);
    renderer.updateZombies(replicated);
    require(render("replaced") == predicted,
        "Replacing authoritative mist must preserve the pending local effect exactly");

    renderer.setLocalBloodMist(local.sample(localStart + .4));
    const auto aged = render("aging");
    require(aged != predicted && tinted(empty, aged, width / 3, width * 2 / 3) > 100,
        "Local hit mist must keep aging visibly while the server animation clock is frozen");
    renderer.setLocalBloodMist(local.sample(localStart + viewer::BloodMist::duration + .1));
    require(render("ended") == empty,
        "Expired local mist must restore the wall pixels while the server animation clock remains frozen");
    viewer::Renderer::checkErrors("local and replicated blood mist");
}

void check(const std::filesystem::path& captures) {
    const auto model = room();
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    viewer::WeaponOverlay overlay(VIEWER_SHADER_DIR, VIEWER_WEAPON_ASSETS);
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY);
    renderer.resize(width, height);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    viewer::Camera camera;
    camera.setPosition({0, 0, 0}); camera.setOrientation(-90, 0);
    if (!captures.empty()) std::filesystem::create_directories(captures);
    // Sample a wall patch clear of the weapon silhouettes.
    const auto wallSample = (static_cast<std::size_t>(height / 2) * width + width / 5) * 3;
    for (const bool night : {false, true}) {
        renderer.setNight(night);
        renderer.setFlashlight(false); // Muzzle illumination is independent of the held flashlight.
        for (std::size_t slot = 0; slot < 4; ++slot) {
            weapons.equip(slot);
            viewer::MuzzleFlash pulse;
            pulse.fire(1, overlay.muzzlePosition(slot, weapons.equipped().config.muzzlePosition));
            const auto render = [&](const viewer::MuzzleFlashLight& flash, const char* phase) {
                renderer.setMuzzleFlash(flash.transformed(glm::inverse(camera.view())));
                renderer.render(camera);
                const auto scene = read();
                overlay.draw(slot, width, height, flash);
                if (!captures.empty()) renderer.writeScreenshot(captures /
                    (std::string(night ? "night-" : "day-") + std::to_string(slot + 1) + "-" + phase + ".ppm"));
                return std::make_pair(scene, read());
            };
            const auto baseline = render({}, "off");
            const auto peak = render(pulse.light(1), "peak");
            const auto faded = render(pulse.light(1.04), "fading");
            const auto ended = render(pulse.light(1.08), "ended");
            require(peak.first[wallSample] > faded.first[wallSample] && faded.first[wallSample] > baseline.first[wallSample],
                    "Nearby wall must light up and fade in both daylight and night");
            require(ended == baseline, "Expired light must restore scene and weapon pixels exactly");
            auto farLight = pulse.light(1); farLight.position.x += 30;
            require(render(farLight, "out-of-range") == baseline, "Point light must have a bounded world-space range");
            std::size_t brightenedWeapon = 0;
            for (std::size_t i = 0; i < peak.second.size(); i += 3) {
                const bool weapon = baseline.second[i] != baseline.first[i] ||
                    baseline.second[i+1] != baseline.first[i+1] || baseline.second[i+2] != baseline.first[i+2];
                if (weapon && peak.second[i] > baseline.second[i]) ++brightenedWeapon;
            }
            require(brightenedWeapon > 10, "Muzzle light must also illuminate visible weapon surfaces");
            std::cout << (night ? "night" : "day") << " slot " << slot + 1 << ": wall red "
                      << int(baseline.first[wallSample]) << " -> " << int(peak.first[wallSample]) << " -> "
                      << int(faded.first[wallSample]) << ", " << brightenedWeapon << " weapon pixels lit\n";
        }
    }
    renderer.loadGrenadeModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) / "he_grenade.glb");
    renderer.setNight(true);
    renderer.setFlashlight(false);
    renderer.setMuzzleFlash({});
    renderer.render(camera);
    const auto empty = read();
    renderer.updateGrenades({{1, {0, -.05f, -.45f}, glm::angleAxis(glm::radians(20.f), glm::vec3(1, 0, 0))}});
    renderer.render(camera);
    const auto grenade = read();
    std::size_t changed = 0;
    for (std::size_t i = 0; i < grenade.size(); ++i) changed += grenade[i] != empty[i];
    if (changed < 1000) throw std::runtime_error("Thrown grenade mesh must render visibly in the world");
    if (!captures.empty()) renderer.writeScreenshot(captures / "grenade-world.ppm");
    renderer.updateGrenades({});
    const viewer::GrenadeExplosion event{1, {0, 0, -1.5f}, 0};
    renderer.setExplosionLights({viewer::grenadeExplosionLight(event)});
    renderer.render(camera);
    const auto explosion = read();
    require(explosion[wallSample] > empty[wallSample] + 50, "Grenade explosion must illuminate the scene with the flashlight off");
    if (!captures.empty()) renderer.writeScreenshot(captures / "grenade-explosion.ppm");
    renderer.setExplosionLights({viewer::grenadeExplosionLight({1, event.position, .09f})});
    renderer.render(camera);
    const auto fadingExplosion = read();
    require(fadingExplosion[wallSample] < explosion[wallSample] && fadingExplosion[wallSample] > empty[wallSample],
            "Explosion illumination must fade between its peak and expiry");
    renderer.setMuzzleFlash({{.1f, -.2f, -.7f}, {7, 3, .7f}, 8});
    renderer.render(camera);
    require(read()[wallSample] > fadingExplosion[wallSample], "Gun muzzle and explosion lights must work simultaneously");
    renderer.setMuzzleFlash({});
    renderer.setExplosionLights({viewer::grenadeExplosionLight({1, event.position, .2f})});
    renderer.render(camera);
    require(read() == empty, "Detonation removes the grenade mesh and its expired light completely");
    viewer::Renderer::checkErrors("muzzle flash scene and weapon");
    localBloodMist(captures);
}
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "muzzle flash check", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    viewer::initializeOpenGL();
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        check(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
