#include "network.hpp"
#include "prediction.hpp"
#include "snapshots.hpp"
#include "camera.hpp"
#ifdef MP_GRAPHICS
#include "players.hpp"
#include "renderer.hpp"
#include "reticle_overlay.hpp"
#include "weapon_overlay.hpp"
#include <GLFW/glfw3.h>
#else
struct GLFWwindow;
#endif
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <map>
#include <thread>

namespace {
struct Input {
    viewer::Camera camera;
    bool capture = true, firstMouse = true, interaction = false, prone = false, reload = false;
    bool leftPressed = false, rightPressed = false;
    bool canScope = false;
    bool flashlight = true;
    double lastX = 0, lastY = 0;
    glm::vec2 drag{0};
    std::uint8_t slot = 0;
};
#ifdef MP_GRAPHICS
Input& input(GLFWwindow* window) { return *static_cast<Input*>(glfwGetWindowUserPointer(window)); }
void key(GLFWwindow* window, int code, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto& in = input(window);
    if (code == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, true);
    if (code == GLFW_KEY_TAB) in.interaction = !in.interaction;
    if (code == GLFW_KEY_Z) in.prone = true;
    if (code == GLFW_KEY_R) in.reload = true;
    if (code == GLFW_KEY_F) in.flashlight = !in.flashlight;
    if (code >= GLFW_KEY_1 && code <= GLFW_KEY_5) in.slot = static_cast<std::uint8_t>(code - GLFW_KEY_1);
    if (code == GLFW_KEY_LEFT_ALT) {
        in.capture = !in.capture; in.firstMouse = true; in.drag = {};
        in.leftPressed = in.rightPressed = false;
        glfwSetInputMode(window, GLFW_CURSOR, in.capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}
void mouse(GLFWwindow* window, double x, double y) {
    auto& in = input(window);
    if (!in.capture) return;
    if (in.firstMouse) { in.firstMouse = false; in.lastX = x; in.lastY = y; return; }
    const float dx = static_cast<float>(x - in.lastX), dy = static_cast<float>(in.lastY - y);
    in.lastX = x; in.lastY = y;
    in.camera.look(dx, dy);
    in.drag = glm::clamp(in.drag + glm::vec2(dx, dy), glm::vec2(-2000), glm::vec2(2000));
}
void button(GLFWwindow* window, int buttonCode, int action, int) {
    auto& in = input(window);
    if (!in.capture || action != GLFW_PRESS) return;
    if (buttonCode == GLFW_MOUSE_BUTTON_LEFT) in.leftPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.interaction) in.rightPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.canScope && !in.interaction) in.camera.cycleViewMode();
}
void focus(GLFWwindow* window, int focused) {
    if (!focused) {
        auto& in = input(window); in.capture = false; in.firstMouse = true;
        in.drag = {}; in.prone = in.reload = false;
        in.leftPressed = in.rightPressed = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
#endif
struct Graphics {
    GLFWwindow* window = nullptr;
#ifdef MP_GRAPHICS
    std::unique_ptr<viewer::Renderer> renderer;
    std::unique_ptr<viewer::ReticleOverlay> reticle;
    std::unique_ptr<viewer::WeaponOverlay> weapon;
    viewer::MuzzleFlash muzzleFlash;
    std::unique_ptr<multiplayer::PlayerRenderer> players;
    ~Graphics() {
        players.reset(); weapon.reset(); reticle.reset(); renderer.reset();
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
    void initialize(multiplayer::Scene& scene, Input& in, bool hidden) {
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; --headless is available for network checks");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(1280, 720, "Gödöllő Multiplayer", nullptr, nullptr);
        if (!window) throw std::runtime_error("Cannot create OpenGL 3.3 window");
        glfwMakeContextCurrent(window); glfwSwapInterval(1);
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;
        glfwSetWindowUserPointer(window, &in); glfwSetKeyCallback(window, key); glfwSetCursorPosCallback(window, mouse);
        glfwSetMouseButtonCallback(window, button); glfwSetWindowFocusCallback(window, focus);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        const auto shaders = multiplayer::resourceRoot() / "shaders";
        renderer = std::make_unique<viewer::Renderer>(scene.model, shaders, scene.trees ? &*scene.trees : nullptr,
            &scene.zombies, scene.forests ? &*scene.forests : nullptr);
        reticle = std::make_unique<viewer::ReticleOverlay>(shaders);
        const viewer::FPSConfig body;
        weapon = std::make_unique<viewer::WeaponOverlay>(shaders, multiplayer::weaponModelDirectory(),
            viewer::WeaponViewBody{body.height, body.eyeHeight});
        renderer->loadGrenadeModel(multiplayer::weaponModelDirectory() / "he_grenade.glb");
        players = std::make_unique<multiplayer::PlayerRenderer>();
    }
#else
    void initialize(multiplayer::Scene&, Input&, bool) { throw std::runtime_error("This build supports --headless clients only"); }
#endif
    bool closed() const {
#ifdef MP_GRAPHICS
        return window && glfwWindowShouldClose(window);
#else
        return false;
#endif
    }
};
multiplayer::Command command(Input& in, GLFWwindow* window, bool bot) {
    multiplayer::Command c;
    c.movement.yawDegrees = in.camera.yawDegrees(); c.pitch = in.camera.pitchDegrees(); c.slot = in.slot;
    c.interaction = in.interaction;
    if (bot) { c.movement.forward = 1; c.movement.yawDegrees = 0; c.pitch = 0; return c; }
    if (!window || !in.capture) return c;
#ifdef MP_GRAPHICS
    const auto held = [&](int code) { return glfwGetKey(window, code) == GLFW_PRESS ? 1.0f : 0.0f; };
    c.movement.forward = held(GLFW_KEY_W) - held(GLFW_KEY_S); c.movement.right = held(GLFW_KEY_D) - held(GLFW_KEY_A);
    c.movement.run = held(GLFW_KEY_LEFT_SHIFT); c.movement.jump = held(GLFW_KEY_SPACE); c.movement.crouch = held(GLFW_KEY_LEFT_CONTROL);
    c.movement.toggleProne = in.prone; in.prone = false;
    c.reload = in.reload; in.reload = false; c.drag = in.drag; in.drag = {};
    c.left = in.leftPressed || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    c.right = c.interaction && (in.rightPressed || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    in.leftPressed = in.rightPressed = false;
#endif
    return c;
}
}
int main(int argc, char** argv) {
    try {
        const auto options = multiplayer::parseOptions(argc, argv, false);
        multiplayer::Scene scene(options);
        for (auto& instance : scene.zombies.instances) instance.removed = true;
        multiplayer::Prediction prediction;
        multiplayer::Snapshots snapshots;
        Input in; in.camera.setPosition(scene.spawnEye); in.camera.setOrientation(options.look.x, options.look.y);
        Graphics graphics;
        if (!options.headless) graphics.initialize(scene, in, options.hidden);
        multiplayer::Network network(options.fakeLagMs, options.fakeLossPercent);
        const auto connection = network.connect(options.address);
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now(); auto previous = start;
        double accumulator = 0, lastSnapshot = 0;
        std::uint32_t self = 0;
        std::size_t received = 0, mostPlayers = 0;
        std::map<std::uint32_t, glm::vec3> remoteOrigins;
        float remoteTravel = 0;
        glm::vec3 visualOffset(0);
        multiplayer::PlayerState ownState;
        auto weaponDefinitions = scene.initialWeapons;
        while (!graphics.closed()) {
            const auto clockNow = Clock::now();
            const double now = std::chrono::duration<double>(clockNow - start).count();
            if (options.seconds && now >= options.seconds) break;
            const double dt = std::min(.1, std::chrono::duration<double>(clockNow - previous).count()); previous = clockNow;
#ifdef MP_GRAPHICS
            if (graphics.window) glfwPollEvents();
#endif
            const auto messages = network.receive();
            for (const auto& event : network.events) {
                if (event.state == k_ESteamNetworkingConnectionState_Connected) {
                    if (!network.send(connection, multiplayer::Hello{scene.fingerprint}, true)) throw std::runtime_error("Cannot send handshake");
                } else if (event.state == k_ESteamNetworkingConnectionState_ClosedByPeer || event.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
                    throw std::runtime_error("Connection ended: " + event.reason);
            }
            for (const auto& [c, message] : messages) {
                (void)c;
                if (const auto* welcome = std::get_if<multiplayer::Welcome>(&message)) {
                    if (self) throw std::runtime_error("Duplicate welcome");
                    self = welcome->player;
                    std::cout << "Connected as player " << self << std::endl;
                } else if (const auto* snapshot = std::get_if<multiplayer::Snapshot>(&message)) {
                    if (!self || !snapshots.push(*snapshot, now)) continue;
                    lastSnapshot = now; ++received; mostPlayers = std::max(mostPlayers, snapshot->players.size());
                    for (const auto& p : snapshot->players) if (p.id != self) {
                        const auto origin = remoteOrigins.emplace(p.id, p.movement.feet).first->second;
                        remoteTravel = std::max(remoteTravel, glm::length(p.movement.feet - origin));
                    }
                    snapshots.apply(scene, now);
                    bool found = false;
                    for (const auto& p : snapshot->players) if (p.id == self) {
                        const bool ready = prediction.ready(); const auto before = prediction.eye();
                        if (!ready || p.slot != ownState.slot) {
                            if (!weaponDefinitions.equip(p.slot))
                                throw std::runtime_error("Server equipped an unavailable weapon");
                            while (in.camera.viewMode() != viewer::Camera::ViewMode::original)
                                in.camera.cycleViewMode();
                        }
                        in.canScope = weaponDefinitions.equipped().config.hasScope;
#ifdef MP_GRAPHICS
                        if (graphics.weapon && ready && p.shots > ownState.shots && !weaponDefinitions.equipped().config.grenade)
                            graphics.muzzleFlash.fire(now, graphics.weapon->muzzlePosition(
                                p.slot, weaponDefinitions.equipped().config.muzzlePosition));
#endif
                        prediction.reconcile(p, *scene.collision); ownState = p; found = true;
                        if (!ready) in.slot = p.slot;
                        const auto correction = before - prediction.eye();
                        if (ready && glm::length(correction) < 2) visualOffset += correction; else visualOffset = {};
                    }
                    if (!found) throw std::runtime_error("Server omitted local player state");
                } else if (const auto* pose = std::get_if<multiplayer::CorpsePose>(&message)) {
                    snapshots.pushPose(scene, *pose);
                    if (!network.send(connection, multiplayer::PoseAck{pose->id, pose->version}, true))
                        throw std::runtime_error("Cannot acknowledge corpse pose");
                } else throw std::runtime_error("Unexpected server message");
            }
            if (now - lastSnapshot > (prediction.ready() ? 5 : 15)) throw std::runtime_error("Server snapshot timeout");
            if (prediction.ready()) {
                accumulator += dt;
                while (accumulator >= 1.0 / 60) {
                    prediction.advance(command(in, graphics.window, options.bot), *scene.collision);
                    auto outgoing = prediction.commandsToSend(); outgoing.snapshotTick = snapshots.latest().tick;
                    network.send(connection, outgoing, false);
                    accumulator -= 1.0 / 60;
                }
            }
            snapshots.apply(scene, now);
            visualOffset *= static_cast<float>(std::exp(-15 * dt));
            if (prediction.ready()) in.camera.setPosition(prediction.eye() + visualOffset);
            if (options.bot) in.camera.setOrientation(0, 0);
#ifdef MP_GRAPHICS
            if (graphics.window) {
                int width = 0, height = 0; glfwGetFramebufferSize(graphics.window, &width, &height);
                if (width > 0 && height > 0) {
                    auto& renderer = *graphics.renderer;
                    renderer.setFlashlight(in.flashlight);
                    renderer.updateGrenades(snapshots.grenades());
                    renderer.updateVegetationBlasts(snapshots.explosions(now));
                    renderer.setExplosionLights(snapshots.explosionLights(now));
                    const auto flash = graphics.muzzleFlash.light(now);
                    renderer.setMuzzleFlash(flash.transformed(glm::inverse(in.camera.view())));
                    renderer.resize(width, height); renderer.updateDoors(*scene.doors); renderer.updateBoxes(*scene.boxes);
                    renderer.updateZombies(scene.zombies);
                    renderer.render(in.camera, snapshots.animationSeconds(), [&] {
                        graphics.players->draw(in.camera, snapshots.players(), self, width, height);
                    });
                    if (prediction.ready() && !in.interaction &&
                        in.camera.viewMode() == viewer::Camera::ViewMode::original &&
                        viewer::weaponViewVisible(weaponDefinitions.equipped().config, ownState.loaded, ownState.weaponCoolingDown))
                        graphics.weapon->draw(ownState.slot, width, height, flash);
                    graphics.reticle->draw(in.camera.viewMode(), width, height);
                    renderer.present(width, height); glfwSwapBuffers(graphics.window);
                    viewer::Renderer::checkErrors("multiplayer frame");
                    const auto title = "Gödöllő Multiplayer | Player " + std::to_string(self) + " | " +
                        (in.interaction ? "Interaction" : "Combat") + " | Ammo " + std::to_string(ownState.loaded) + "/" + std::to_string(ownState.reserve);
                    glfwSetWindowTitle(graphics.window, title.c_str());
                }
            } else
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!options.screenshot.empty()) {
#ifdef MP_GRAPHICS
            if (!graphics.renderer) throw std::runtime_error("Screenshot needs a graphical client");
            graphics.renderer->writeScreenshot(options.screenshot);
#else
            throw std::runtime_error("Screenshot needs a graphical build");
#endif
        }
        const auto feet = prediction.controller().feet();
        std::cout << "CLIENT player=" << self << " snapshots=" << received << " max_players=" << mostPlayers
            << " resolved=" << ownState.resolved << " outstanding=" << prediction.outstanding()
            << " feet=" << feet.x << ',' << feet.y << ',' << feet.z << " remote_travel=" << remoteTravel << '\n';
        if (!prediction.ready()) throw std::runtime_error("Session ended before spawn");
        network.send(connection, multiplayer::Goodbye{}, true);
        // Give the server a bounded opportunity to acknowledge departure and release grabs.
        const auto deadline = Clock::now() + std::chrono::milliseconds(500);
        while (Clock::now() < deadline) {
            network.receive();
            bool closed = false;
            for (const auto& event : network.events)
                closed |= event.state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                          event.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
            if (closed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << "Client: " << error.what() << '\n'; return 1; }
}
