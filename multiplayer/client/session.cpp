#include "session.hpp"
#include "network.hpp"
#include "prediction.hpp"
#include "snapshots.hpp"
#include "camera.hpp"
#include "weapon_motion.hpp"

#include "vehicle_player.hpp"
#ifdef MP_GRAPHICS
#include "players.hpp"
#include "renderer.hpp"
#include "reticle_overlay.hpp"
#include "hud.hpp"
#include "minimap.hpp"
#include "weapon_overlay.hpp"
#include "player_body_renderer.hpp"
#include "undergrowth_audio.hpp"
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
#include <future>

namespace {
struct Input {
    viewer::Camera camera;
    viewer::WeaponMotion weaponMotion;
    std::size_t equippedSlot = 0; // Authoritative slot, independent of requested slot.

    viewer::VehiclePlayer driver;
    bool enterVehicle = false, toggleDrivingCamera = false;
    bool capture = true, firstMouse = true, interaction = false, prone = false, reload = false;
    bool leftPressed = false, rightPressed = false;
    bool canScope = false;
    bool flashlight = true;
    bool leave = false;
    double lastX = 0, lastY = 0;
    glm::vec2 drag{0};
    std::uint8_t slot = 0;
};
#ifdef MP_GRAPHICS
Input& input(GLFWwindow* window) { return *static_cast<Input*>(glfwGetWindowUserPointer(window)); }
void key(GLFWwindow* window, int code, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto& in = input(window);
    if (code == GLFW_KEY_ESCAPE) in.leave = true;
    if (code == GLFW_KEY_ENTER && in.capture) in.enterVehicle = true;
    if ((code == GLFW_KEY_LEFT_ALT || code == GLFW_KEY_RIGHT_ALT) && in.driver.driving()) in.toggleDrivingCamera = true;
    if (in.driver.occupied() && code != GLFW_KEY_F12) return;
    if (code == GLFW_KEY_TAB) in.interaction = !in.interaction;
    if (code == GLFW_KEY_Z) in.prone = true;
    if (code == GLFW_KEY_R) in.reload = true;
    if (code == GLFW_KEY_F) in.flashlight = !in.flashlight;
    if (code == GLFW_KEY_GRAVE_ACCENT) in.slot = static_cast<std::uint8_t>(viewer::punchSlot);
    if (code >= GLFW_KEY_1 && code <= GLFW_KEY_5) in.slot = static_cast<std::uint8_t>(code - GLFW_KEY_1);
    if (code == GLFW_KEY_LEFT_ALT || code == GLFW_KEY_F12) {
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
    if (!in.capture || in.driver.occupied() || action != GLFW_PRESS) return;
    if (buttonCode == GLFW_MOUSE_BUTTON_LEFT) in.leftPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.interaction) in.rightPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.canScope && !in.interaction) in.camera.cycleViewMode();
}
void focus(GLFWwindow* window, int focused) {
    if (!focused) {
        auto& in = input(window); in.capture = false; in.firstMouse = true;
        in.drag = {}; in.prone = in.reload = in.enterVehicle = in.toggleDrivingCamera = false;
        in.leftPressed = in.rightPressed = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
#endif
struct Graphics {
    GLFWwindow* window = nullptr;
    bool ownsWindow = false;
#ifdef MP_GRAPHICS
    std::unique_ptr<viewer::Renderer> renderer;
    std::unique_ptr<viewer::ReticleOverlay> reticle;
    std::unique_ptr<viewer::Hud> hud;
    float speedMetersPerSecond = 0;
    std::unique_ptr<viewer::WeaponOverlay> weapon;
    std::unique_ptr<viewer::AudioSystem> audio;
    viewer::UndergrowthEmitter undergrowth;
    viewer::MuzzleFlash muzzleFlash;
    std::size_t muzzleSlot = 0;
    glm::vec3 muzzleModelPosition{0};
    std::unique_ptr<multiplayer::PlayerRenderer> players;
    std::unique_ptr<viewer::PlayerBodyRenderer> body;
    ~Graphics() {
        audio.reset(); body.reset(); players.reset(); weapon.reset(); hud.reset(); reticle.reset(); renderer.reset();
        if (window) {
            glfwSetWindowUserPointer(window, nullptr);
            glfwSetKeyCallback(window, nullptr); glfwSetCursorPosCallback(window, nullptr);
            glfwSetCharCallback(window, nullptr);
            glfwSetMouseButtonCallback(window, nullptr); glfwSetWindowFocusCallback(window, nullptr);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        if (ownsWindow) { glfwDestroyWindow(window); glfwTerminate(); }
    }
    void initialize(multiplayer::Scene& scene, Input& in, bool hidden, bool sound, GLFWwindow* sharedWindow) {
        window = sharedWindow;
        if (!window) {
            if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; --headless is available for network checks");
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = glfwCreateWindow(1280, 720, "Godollo", nullptr, nullptr);
            if (!window) { glfwTerminate(); throw std::runtime_error("Cannot create OpenGL 3.3 window"); }
            ownsWindow = true;
        }
        glfwMakeContextCurrent(window); glfwSwapInterval(1);
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;
        bindInput(in);
        const auto shaders = multiplayer::resourceRoot() / "shaders";
        renderer = std::make_unique<viewer::Renderer>(scene.model, shaders, scene.trees ? &*scene.trees : nullptr,
            &scene.zombies, scene.forests ? &*scene.forests : nullptr);
        renderer->setVehicleAssets(multiplayer::resourceRoot() / "assets/vehicles/models");
        reticle = std::make_unique<viewer::ReticleOverlay>(shaders);
        hud = std::make_unique<viewer::Hud>(multiplayer::resourceRoot() / "ui");
        if (const auto minimap = viewer::findMinimapSource(scene.model)) {
            hud->setMinimap(*minimap, renderer->sceneTexture(minimap->texture));
            hud->setMinimapRoads(scene.model);
        }
        const viewer::FPSConfig playerConfig;
        weapon = std::make_unique<viewer::WeaponOverlay>(shaders, multiplayer::weaponModelDirectory(),
            viewer::WeaponViewBody{playerConfig.height, playerConfig.eyeHeight});
        renderer->loadGrenadeModel(multiplayer::weaponModelDirectory() / "he_grenade.glb");
        players = std::make_unique<multiplayer::PlayerRenderer>();
        body = std::make_unique<viewer::PlayerBodyRenderer>(shaders);
        viewer::AudioOptions audioOptions;
        audioOptions.directory = multiplayer::audioDirectory();
        audioOptions.enabled = sound;
        audioOptions.ambience = false;
        audio = std::make_unique<viewer::AudioSystem>(audioOptions);
    }
    void bindInput(Input& in) {
        glfwSetWindowUserPointer(window, &in); glfwSetKeyCallback(window, key); glfwSetCursorPosCallback(window, mouse);
        glfwSetMouseButtonCallback(window, button); glfwSetWindowFocusCallback(window, focus);
        glfwSetCharCallback(window, nullptr);
        glfwSetInputMode(window, GLFW_CURSOR, in.capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
#else
    void bindInput(Input&) {}
    void initialize(multiplayer::Scene&, Input&, bool, bool, GLFWwindow*) { throw std::runtime_error("This build supports --headless clients only"); }
#endif
    bool closed() const {
#ifdef MP_GRAPHICS
        return window && glfwWindowShouldClose(window);
#else
        return false;
#endif
    }
};
multiplayer::Command command(Input& in, GLFWwindow* window, bool bot, double seconds) {
    multiplayer::Command c;
    const auto aim = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, seconds, !in.driver.occupied() && !in.interaction);
    c.movement.yawDegrees = aim.yawDegrees(); c.pitch = aim.pitchDegrees(); c.slot = in.slot;
    c.interaction = in.interaction;
    if (bot) { c.movement.forward = 1; c.movement.yawDegrees = 0; c.pitch = 0; return c; }
    if (!window || !in.capture) return c;
#ifdef MP_GRAPHICS
    c.enterVehicle = in.enterVehicle; in.enterVehicle = false;
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
int multiplayer::runClientSession(const Options& options, GLFWwindow* window,
                                  const SessionFeedback& feedback, const std::function<void()>& checkServer) {
    std::unique_ptr<Scene> loaded;
    if (feedback) {
        auto loading = std::async(std::launch::async, [&] { return std::make_unique<Scene>(options); });
        bool cancelled = false;
        while (loading.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            if (!feedback(cancelled ? "Cancelling - finishing scene preparation..." : "Loading scene...")) cancelled = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        loaded = loading.get();
        if (cancelled) return 0;
    } else loaded = std::make_unique<Scene>(options);
    auto& scene = *loaded;
    for (auto& instance : scene.zombies.instances) instance.removed = true;
    multiplayer::Prediction prediction;
    multiplayer::Snapshots snapshots;
    Input in; in.camera.setPosition(scene.spawnEye); in.camera.setOrientation(options.look.x, options.look.y);
    Graphics graphics;
    if (!options.headless) graphics.initialize(scene, in, options.hidden, options.audio, window);
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
    while (!graphics.closed() && !in.leave) {
        if (checkServer) checkServer();
        const auto clockNow = Clock::now();
        const double now = std::chrono::duration<double>(clockNow - start).count();
        if (options.seconds && now >= options.seconds) break;
        const double dt = std::min(.1, std::chrono::duration<double>(clockNow - previous).count()); previous = clockNow;
#ifdef MP_GRAPHICS
        if (graphics.window) glfwPollEvents();
#endif
        if (!prediction.ready() && feedback) {
            if (!feedback("Connecting - waiting for player state...")) break;
            graphics.bindInput(in);
        }
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
#ifdef MP_GRAPHICS
                graphics.undergrowth = viewer::UndergrowthEmitter(self);
#endif
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
                    if (!ready || p.slot != ownState.slot || p.vehicle != ownState.vehicle || p.seat != ownState.seat) {
                        if (!weaponDefinitions.equip(p.slot))
                            throw std::runtime_error("Server equipped an unavailable weapon");
                        while (in.camera.viewMode() != viewer::Camera::ViewMode::original)
                            in.camera.cycleViewMode();
                    }
                    in.canScope = p.vehicle == multiplayer::noVehicle && weaponDefinitions.equipped().config.hasScope;
                    in.equippedSlot = p.slot;
#ifdef MP_GRAPHICS
                    if (graphics.weapon && ready && p.shots > ownState.shots && p.slot != viewer::punchSlot && !weaponDefinitions.equipped().config.grenade) {
                        in.weaponMotion.fire(p.slot, now);
                        graphics.muzzleSlot = p.slot;
                        graphics.muzzleModelPosition = weaponDefinitions.equipped().config.muzzlePosition;
                        graphics.muzzleFlash.fire(now, {});
                    }
#endif
                    prediction.reconcile(p,*scene.collision);
                    const bool walking=p.vehicle==multiplayer::noVehicle && ownState.vehicle==multiplayer::noVehicle;
                    in.driver.followCar(p.vehicle==multiplayer::noVehicle?std::optional<std::size_t>{}:p.vehicle,
                                        *scene.vehicles,in.camera,p.seat==multiplayer::noVehicleSeat?0:p.seat);
                    ownState=p; found=true;
                    if (!ready) {
                        in.slot = p.slot;
                        if (!options.explicitLook) in.camera.setOrientation(p.yaw, p.pitch);
                    }
                    const auto correction = before - prediction.eye();
                    if (ready && walking && glm::length(correction) < 2) visualOffset += correction; else visualOffset = {};
                }
                if (!found) throw std::runtime_error("Server omitted local player state");
            } else if (const auto* pose = std::get_if<multiplayer::CorpsePose>(&message)) {
                snapshots.pushPose(scene, *pose);
                if (!network.send(connection, multiplayer::PoseAck{pose->id, pose->version}, true))
                    throw std::runtime_error("Cannot acknowledge corpse pose");
            } else throw std::runtime_error("Unexpected server message");
        }
        if (now - lastSnapshot > (prediction.ready() ? 5 : 15)) throw std::runtime_error(prediction.ready() ? "Connection lost: server stopped responding (snapshot timeout)" :
                                     "Could not join: server did not provide player state within 15 seconds");
        if (prediction.ready()) {
            accumulator += dt;
            while (accumulator >= 1.0 / 60) {
                const auto nextCommand = command(in, graphics.window, options.bot, now);
#ifdef MP_GRAPHICS
                const auto& controller = prediction.controller();
                const viewer::UndergrowthMotion shrubBefore{controller.feet(), controller.bodyHeight(),
                    controller.config().radius, controller.grounded(), false};
#endif
                prediction.advance(nextCommand, *scene.collision);
#ifdef MP_GRAPHICS
                const auto movement = controller.feet() - shrubBefore.feet;
                graphics.speedMetersPerSecond = glm::length(glm::vec2(movement.x, movement.z)) / multiplayer::tickSeconds;
                // Fresh prediction only: reconciliation replays must stay silent.
                if (graphics.audio) {
                    const bool running = nextCommand.movement.run && controller.stance() == viewer::PlayerStance::Standing;
                    if (const auto event = graphics.undergrowth.update(multiplayer::tickSeconds, shrubBefore,
                            {controller.feet(), controller.bodyHeight(), controller.config().radius, controller.grounded(), running},
                            scene.undergrowth))
                        viewer::playUndergrowth(*graphics.audio, *event);
                }
#endif
                auto outgoing = prediction.commandsToSend(); outgoing.snapshotTick = snapshots.latest().tick;
                network.send(connection, outgoing, false);
                accumulator -= 1.0 / 60;
            }
        }
        snapshots.apply(scene, now);
        visualOffset *= static_cast<float>(std::exp(-15 * dt));
        if (prediction.ready()) {
            if (in.toggleDrivingCamera) {
                in.driver.toggleThirdPerson(*scene.vehicles, in.camera, *scene.collision);
                in.toggleDrivingCamera = false;
            }
            if (in.driver.occupied()) in.driver.updateCamera(*scene.vehicles, in.camera, *scene.collision);
            else in.camera.setPosition(prediction.eye() + visualOffset);
        }
        if (options.bot) in.camera.setOrientation(0, 0);
#ifdef MP_GRAPHICS
        if (graphics.window && (!feedback || prediction.ready())) {
            graphics.audio->setListener(in.camera.position(), in.camera.direction());
            graphics.audio->update();
            int width = 0, height = 0; glfwGetFramebufferSize(graphics.window, &width, &height);
            if (width > 0 && height > 0) {
                auto& renderer = *graphics.renderer;
                renderer.setFlashlight(in.flashlight);
                renderer.updateGrenades(snapshots.grenades());
                renderer.updateVegetationBlasts(snapshots.explosions(now));
                renderer.setExplosionLights(snapshots.explosionLights(now));
                const auto viewCamera = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, now, !in.driver.occupied() && !in.interaction);
                auto flash = graphics.muzzleFlash.light(now);
                flash.position = graphics.weapon->muzzlePosition(graphics.muzzleSlot, graphics.muzzleModelPosition,
                    in.weaponMotion.pose(graphics.muzzleSlot, now));
                renderer.setMuzzleFlash(flash.transformed(glm::inverse(viewCamera.view())));
                renderer.resize(width, height); renderer.updateDoors(*scene.doors); renderer.updateBoxes(*scene.boxes);
                renderer.updateVehicles(*scene.vehicles, in.driver.car());
                renderer.setDayNightLighting(viewer::dayNightLighting(scene.dayNight.hourAt(snapshots.animationSeconds())));
                renderer.updateZombies(scene.zombies);
                viewer::PlayerBodyPose ownBody;
                if (prediction.ready() && !in.driver.occupied() && !in.interaction && in.slot==viewer::punchSlot && ownState.slot==viewer::punchSlot) {
                    // Local hands use the newest confirmed pose, without the
                    // remote interpolation delay; movement remains predicted.
                    ownBody = ownState.melee;
                    if (ownBody.enabled) {
                        const auto translation = in.camera.position()-ownBody.eye;
                        ownBody.eye += translation;
                        for (auto& part : ownBody.parts) part[3] += glm::vec4(translation,0);
                    }
                }
                graphics.body->capturePose(ownBody,static_cast<float>(dt));
                renderer.render(viewCamera, snapshots.animationSeconds(), [&] {
                    graphics.players->draw(viewCamera, snapshots.players(), self, width, height);
                    graphics.body->drawPose(ownBody,viewCamera.projection(float(width)/height)*viewCamera.view(),width,height);
                });
                if (prediction.ready() && !in.driver.occupied() && !in.interaction &&
                    in.camera.viewMode() == viewer::Camera::ViewMode::original &&
                    viewer::weaponViewVisible(weaponDefinitions.equipped().config, ownState.loaded, ownState.weaponCoolingDown))
                    graphics.weapon->draw(ownState.slot, width, height, flash,
                                          in.weaponMotion.pose(ownState.slot, now));
                if (!in.driver.occupied()) graphics.reticle->draw(in.camera.viewMode(), width, height);
                viewer::HudState hudState;
                hudState.position = in.driver.occupied() ? in.driver.eyePosition(*scene.vehicles) : in.camera.position();
                hudState.yawDegrees = in.camera.yawDegrees();
                const auto stance = prediction.controller().stance();
                hudState.movement = in.driver.occupied() ? (in.driver.driving() ? "DRIVING" : "PASSENGER") :
                    stance == viewer::PlayerStance::Prone ? "PRONE" : stance == viewer::PlayerStance::Crouched ? "CROUCH" : "STAND";
                hudState.mode = in.interaction ? "INTERACT" : "COMBAT";
                if (const auto car = in.driver.car()) {
                    const auto velocity = scene.vehicles->velocity(*car);
                    hudState.speedMetersPerSecond = glm::length(glm::vec2(velocity.x, velocity.z));
                } else hudState.speedMetersPerSecond = graphics.speedMetersPerSecond;
                hudState.mouseCaptured = in.capture;
                if (prediction.ready()) {
                    hudState.equipment = weaponDefinitions.equipped().config.name;
                    hudState.slot = ownState.slot;
                    if (ownState.slot != viewer::punchSlot) {
                        hudState.loaded = static_cast<int>(ownState.loaded);
                        hudState.reserve = static_cast<int>(ownState.reserve);
                    }
                }
                if (in.driver.occupied()) hudState.prompt = "Enter: exit vehicle";
                graphics.hud->draw(hudState, width, height);
                renderer.present(width, height); glfwSwapBuffers(graphics.window);
                viewer::Renderer::checkErrors("multiplayer frame");
                const auto entry = !in.driver.occupied() && prediction.ready() ?
                    viewer::vehicleEntry(*scene.vehicles, in.camera.position(), *scene.collision,
                        [&](std::size_t car, std::size_t seat) {
                            for (const auto& p : snapshots.latest().players)
                                if (p.vehicle == car && p.seat == seat) return false;
                            return true;
                        }) : std::optional<viewer::VehicleSeatRef>{};
                const auto title = "Godollo | Player " + std::to_string(self) + " | " +
                    (in.driver.occupied() ? (in.driver.driving() ? "Driving | WASD, Space brake, Enter exit, Alt camera" : "Passenger | Mouse look, Enter exit") :
                     in.interaction ? "Interaction" : "Combat") + (ownState.slot==viewer::punchSlot ? " | Fists: left click punch" :
                        " | Ammo " + std::to_string(ownState.loaded) + "/" + std::to_string(ownState.reserve))
                    + (entry ? " | " + viewer::vehicleEntryLabel(*scene.vehicles, *entry) : "");
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
    if (!prediction.ready() && !feedback) throw std::runtime_error("Session ended before spawn");
    if (!prediction.ready()) return 0;
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
}
