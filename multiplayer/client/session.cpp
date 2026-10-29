#include "session.hpp"
#include "network.hpp"
#include "prediction.hpp"
#include "snapshots.hpp"
#include "signals.hpp"
#include "boid/replica.hpp"
#include "camera.hpp"
#include "weapon_motion.hpp"
#include "action_selection.hpp"

#include "vehicle_player.hpp"
#include "inventory/inventory_config.hpp"
#ifdef MP_GRAPHICS
#include "players.hpp"
#include "renderer.hpp"
#include "gpu_timer.hpp"
#include "boid/bird_renderer.hpp"
#include "reticle_overlay.hpp"
#include "gui/hud.hpp"
#include "minimap.hpp"
#include "weapon_overlay.hpp"
#include "player_body_renderer.hpp"
#include "audio_playback.hpp"
#include "window.hpp"
#include <GLFW/glfw3.h>
#else
struct GLFWwindow;
#endif
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <map>
#include <thread>
#include <future>
#include <iomanip>
#include <sstream>

namespace {
struct Input {
    viewer::Camera camera;
    viewer::WeaponMotion weaponMotion;
    viewer::ActionSelection actionSelection;
    viewer::ActionShortcuts actions;
    bool suppressLeftUntilRelease = false, weaponSwitching = false, weaponReloading = false;
    std::size_t equippedSlot = 0; // Authoritative slot, independent of requested slot.

    viewer::VehiclePlayer driver;
    bool enterVehicle = false, toggleDrivingCamera = false;
    bool capture = true, firstMouse = true, interaction = false, prone = false, reload = false;
    bool leftPressed = false, rightPressed = false, leftHeld = false;
    bool rightHeld = false, wheelOpen = false, signalMouseSuppressed = false;
    glm::vec2 wheelOffset{0}; // Design dp; same scale as the shared HUD.
    multiplayer::SignalKind pendingSignal = multiplayer::SignalKind::None;
    void cancelSignal() { wheelOpen = false; pendingSignal = multiplayer::SignalKind::None; }
    bool canScope = false;
    bool flashlight = true;
    bool leave = false;
    std::vector<viewer::InventoryDragAction> inventoryActions;
#ifdef MP_GRAPHICS
    viewer::HudScreen hudScreen = viewer::HudScreen::Gameplay;
    viewer::Hud* hud = nullptr; // Borrowed from Graphics on the GL thread.
#endif
    double lastX = 0, lastY = 0;
    glm::vec2 drag{0};
    std::uint8_t slot = 0;
};
#ifdef MP_GRAPHICS
Input& input(GLFWwindow* window) { return *static_cast<Input*>(glfwGetWindowUserPointer(window)); }
void key(GLFWwindow* window, int code, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto& in = input(window);
    if ((code >= GLFW_KEY_F1 && code <= GLFW_KEY_F4) ||
        (code == GLFW_KEY_ESCAPE && in.hudScreen != viewer::HudScreen::Gameplay)) {
        constexpr std::array<viewer::HudScreen, 4> screens{viewer::HudScreen::Inventory, viewer::HudScreen::Skills,
            viewer::HudScreen::Health, viewer::HudScreen::Map};
        const auto requested = code == GLFW_KEY_ESCAPE ? viewer::HudScreen::Gameplay : screens[code - GLFW_KEY_F1];
        in.hudScreen = in.hudScreen == requested ? viewer::HudScreen::Gameplay : requested;
        in.firstMouse = true; in.drag = {};
        in.cancelSignal();
        in.actionSelection.close();
        in.leftPressed = in.rightPressed = in.prone = in.reload = in.enterVehicle = in.toggleDrivingCamera = false;
        if (in.hud) in.hud->cancelPointerInteraction();
        glfwSetInputMode(window, GLFW_CURSOR,
            in.capture && in.hudScreen == viewer::HudScreen::Gameplay ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        return;
    }
    if (in.hudScreen != viewer::HudScreen::Gameplay) return;
    if (code == GLFW_KEY_ESCAPE && in.wheelOpen) { in.cancelSignal(); return; }
    if (in.wheelOpen && code != GLFW_KEY_F12 && code != GLFW_KEY_LEFT_ALT) return;
    if (code == GLFW_KEY_ESCAPE && in.actionSelection.open()) { in.actionSelection.close(); return; }
    if (code == GLFW_KEY_ESCAPE) in.leave = true;
    if (code == GLFW_KEY_ENTER && in.capture) in.enterVehicle = true;
    if ((code == GLFW_KEY_LEFT_ALT || code == GLFW_KEY_RIGHT_ALT) && in.driver.driving()) in.toggleDrivingCamera = true;
    if (in.driver.occupied() && code != GLFW_KEY_F12) return;
    if (code == GLFW_KEY_TAB) { in.interaction = !in.interaction; in.actionSelection.close(); }
    if (code == GLFW_KEY_Z) in.prone = true;
    if (code == GLFW_KEY_R && !in.weaponSwitching && !in.weaponReloading && !in.actionSelection.open()) {
        in.reload = true;
        in.suppressLeftUntilRelease |= in.leftHeld;
    }
    if (code == GLFW_KEY_F) in.flashlight = !in.flashlight;
    if (code == GLFW_KEY_GRAVE_ACCENT || (code >= GLFW_KEY_1 && code <= GLFW_KEY_9)) {
        in.actionSelection.highlight(code == GLFW_KEY_GRAVE_ACCENT ? 0 : code - GLFW_KEY_1 + 1);
        in.leftPressed = false;
    }
    if (code == GLFW_KEY_LEFT_ALT || code == GLFW_KEY_F12) {
        in.cancelSignal();
        in.capture = !in.capture; in.firstMouse = true; in.drag = {};
        in.actionSelection.close();
        in.leftPressed = in.rightPressed = false;
        glfwSetInputMode(window, GLFW_CURSOR, in.capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}
void hudPointer(GLFWwindow* window, viewer::Hud* hud, double x, double y) {
    int width, height; glfwGetWindowSize(window, &width, &height);
    if (hud && width > 0 && height > 0) hud->pointerMove(float(x / width), float(y / height));
}
void mouse(GLFWwindow* window, double x, double y) {
    auto& in = input(window);
    if (in.hudScreen == viewer::HudScreen::Inventory || in.hudScreen == viewer::HudScreen::Skills || in.hudScreen == viewer::HudScreen::Map) {
        hudPointer(window, in.hud, x, y); return;
    }
    if (!in.capture || in.hudScreen != viewer::HudScreen::Gameplay) return;
    if (in.firstMouse) { in.firstMouse = false; in.lastX = x; in.lastY = y; return; }
    const float dx = static_cast<float>(x - in.lastX), dy = static_cast<float>(in.lastY - y);
    in.lastX = x; in.lastY = y;
    if (in.wheelOpen) {
        int width, height; glfwGetWindowSize(window, &width, &height);
        const float scale = std::min(float(width) / 1280, float(height) / 720);
        if (scale > 0) in.wheelOffset += glm::vec2(dx, -dy) / scale;
        const float length = glm::length(in.wheelOffset);
        if (length > 120) in.wheelOffset *= 120 / length;
        return;
    }
    in.camera.look(dx, dy);
    in.drag = glm::clamp(in.drag + glm::vec2(dx, dy), glm::vec2(-2000), glm::vec2(2000));
}
void button(GLFWwindow* window, int buttonCode, int action, int) {
    auto& in = input(window);
    if (buttonCode == GLFW_MOUSE_BUTTON_LEFT) {
        in.leftHeld = action == GLFW_PRESS;
        if (action == GLFW_RELEASE) in.suppressLeftUntilRelease = false;
    }
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT) in.rightHeld = action == GLFW_PRESS;
    if (buttonCode == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS && in.capture && !in.driver.occupied() && in.hudScreen == viewer::HudScreen::Gameplay) {
            in.wheelOpen = true; in.wheelOffset = {}; in.drag = {};
            in.actionSelection.close();
            in.leftPressed = in.rightPressed = in.reload = false;
            in.signalMouseSuppressed = true;
        } else if (action == GLFW_RELEASE && in.wheelOpen) {
            in.pendingSignal = multiplayer::signalWheelSelection(in.wheelOffset);
            in.wheelOpen = false;
        }
        return;
    }
    if (in.hudScreen == viewer::HudScreen::Inventory || in.hudScreen == viewer::HudScreen::Map) {
        double x, y; glfwGetCursorPos(window, &x, &y);
        hudPointer(window, in.hud, x, y);
        if (in.hud) in.hud->pointerButton(buttonCode, action == GLFW_PRESS);
        return;
    }
    if (!in.capture || in.driver.occupied() || in.wheelOpen || in.signalMouseSuppressed || action != GLFW_PRESS || in.hudScreen != viewer::HudScreen::Gameplay) return;
    if (buttonCode == GLFW_MOUSE_BUTTON_LEFT && in.actionSelection.open()) {
        const auto target = in.actionSelection.confirm(in.actions, in.equippedSlot);
        if (target && !in.weaponSwitching && !in.weaponReloading && in.slot == in.equippedSlot) in.slot = static_cast<std::uint8_t>(*target);
        in.leftPressed = false; in.suppressLeftUntilRelease = true;
        return;
    }
    if (in.weaponSwitching || in.weaponReloading || in.slot != in.equippedSlot) {
        if (buttonCode == GLFW_MOUSE_BUTTON_LEFT) in.suppressLeftUntilRelease = true;
        return;
    }
    if (buttonCode == GLFW_MOUSE_BUTTON_LEFT) in.leftPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.interaction) in.rightPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.canScope && !in.interaction) in.camera.cycleViewMode();
}
void scroll(GLFWwindow* window, double, double y) {
    auto& in = input(window);
    if (in.wheelOpen) return;
    if ((in.hudScreen == viewer::HudScreen::Inventory || in.hudScreen == viewer::HudScreen::Skills || in.hudScreen == viewer::HudScreen::Map) && in.hud) {
        double x, cursorY; glfwGetCursorPos(window, &x, &cursorY);
        hudPointer(window, in.hud, x, cursorY);
        in.hud->mouseWheel(static_cast<float>(y));
    }
    if (in.hudScreen == viewer::HudScreen::Gameplay && in.capture && !in.driver.occupied() && !in.interaction && std::isfinite(y) && y != 0) {
        in.actionSelection.cycle(y < 0 ? 1 : -1, in.actions, in.equippedSlot);
        in.leftPressed = false;
    }
}
void focus(GLFWwindow* window, int focused) {
    if (!focused) {
        auto& in = input(window); in.capture = false; in.firstMouse = true;
        in.cancelSignal(); in.rightHeld = false;
        in.actionSelection.close();
        if (in.hud) in.hud->cancelPointerInteraction();
        in.drag = {}; in.prone = in.reload = in.enterVehicle = in.toggleDrivingCamera = false;
        in.leftPressed = in.rightPressed = in.leftHeld = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
#endif
struct Graphics {
    GLFWwindow* window = nullptr;
    bool ownsWindow = false;
#ifdef MP_GRAPHICS
    std::unique_ptr<viewer::Renderer> renderer;
    std::unique_ptr<viewer::GpuTimer> gpuTimer;
    std::unique_ptr<viewer::BirdRenderer> birds;
    std::unique_ptr<viewer::ReticleOverlay> reticle;
    std::unique_ptr<viewer::Hud> hud;
    float speedMetersPerSecond = 0;
    std::unique_ptr<viewer::WeaponOverlay> weapon;
    std::unique_ptr<multiplayer::ClientAudio> audio;
    viewer::MuzzleFlash muzzleFlash;
    std::size_t muzzleSlot = 0;
    glm::vec3 muzzleModelPosition{0};
    std::unique_ptr<multiplayer::PlayerRenderer> players;
    std::unique_ptr<viewer::PlayerBodyRenderer> body;
    ~Graphics() {
        gpuTimer.reset();
        audio.reset(); birds.reset(); body.reset(); players.reset(); weapon.reset(); hud.reset(); reticle.reset(); renderer.reset();
        if (window) {
            glfwSetWindowUserPointer(window, nullptr);
            glfwSetKeyCallback(window, nullptr); glfwSetCursorPosCallback(window, nullptr);
            glfwSetCharCallback(window, nullptr);
            glfwSetScrollCallback(window, nullptr);
            glfwSetMouseButtonCallback(window, nullptr); glfwSetWindowFocusCallback(window, nullptr);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        if (ownsWindow) { glfwDestroyWindow(window); glfwTerminate(); }
    }
    void initialize(multiplayer::Scene& scene, Input& in, const multiplayer::Options& options, GLFWwindow* sharedWindow, viewer::PlayerInventory& inventory) {
        in.actions = viewer::actionShortcuts(inventory);
        window = sharedWindow;
        if (!window) {
            window = multiplayer::createClientWindow(options.display, options.hidden);
            ownsWindow = true;
        }
        glfwMakeContextCurrent(window); glfwSwapInterval(1);
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;
        int width = 0, height = 0; glfwGetFramebufferSize(window, &width, &height);
        const auto renderSize = options.display.renderSize({width, height});
        std::cout << (options.display.fullscreen ? "Fullscreen" : "Windowed") << "; render resolution "
                  << renderSize.x << 'x' << renderSize.y << "; GUI resolution " << width << 'x' << height << '\n';
        bindInput(in);
        const auto shaders = multiplayer::resourceRoot() / "shaders";
        renderer = std::make_unique<viewer::Renderer>(scene.model, shaders, scene.trees ? &*scene.trees : nullptr,
            &scene.zombies, scene.forests ? &*scene.forests : nullptr);
        gpuTimer = std::make_unique<viewer::GpuTimer>();
        renderer->setVehicleAssets(multiplayer::resourceRoot() / "assets/vehicles/models");
        reticle = std::make_unique<viewer::ReticleOverlay>(shaders);
        hud = std::make_unique<viewer::Hud>(multiplayer::resourceRoot() / "ui", inventory);
        in.hud = hud.get();
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
        birds = std::make_unique<viewer::BirdRenderer>(shaders);
        viewer::AudioOptions audioOptions;
        audioOptions.directory = multiplayer::audioDirectory();
        audioOptions.enabled = options.audio;
        audio = std::make_unique<multiplayer::ClientAudio>(audioOptions);
    }
    void bindInput(Input& in) {
        in.hud = hud.get();
        glfwSetWindowUserPointer(window, &in); glfwSetKeyCallback(window, key); glfwSetCursorPosCallback(window, mouse);
        glfwSetMouseButtonCallback(window, button); glfwSetWindowFocusCallback(window, focus);
        glfwSetCharCallback(window, nullptr);
        glfwSetScrollCallback(window, scroll);
        glfwSetInputMode(window, GLFW_CURSOR,
            in.capture && in.hudScreen == viewer::HudScreen::Gameplay ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
#else
    void bindInput(Input&) {}
    void initialize(multiplayer::Scene&, Input&, const multiplayer::Options&, GLFWwindow*, viewer::PlayerInventory&) { throw std::runtime_error("This build supports --headless clients only"); }
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
    c.inventoryActions.swap(in.inventoryActions);
    const auto aim = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, seconds, !in.driver.occupied() && !in.interaction);
    c.movement.yawDegrees = aim.yawDegrees(); c.pitch = aim.pitchDegrees(); c.slot = in.slot;
    c.interaction = in.interaction;
    if (bot) { c.movement.forward = 1; c.movement.yawDegrees = 0; c.pitch = 0; return c; }
    if (!window || !in.capture) return c;
#ifdef MP_GRAPHICS
    if (in.hudScreen != viewer::HudScreen::Gameplay) return c;
    c.enterVehicle = in.enterVehicle; in.enterVehicle = false;
    const auto held = [&](int code) { return glfwGetKey(window, code) == GLFW_PRESS ? 1.0f : 0.0f; };
    c.movement.forward = held(GLFW_KEY_W) - held(GLFW_KEY_S); c.movement.right = held(GLFW_KEY_D) - held(GLFW_KEY_A);
    c.movement.run = held(GLFW_KEY_LEFT_SHIFT); c.movement.jump = held(GLFW_KEY_SPACE); c.movement.crouch = held(GLFW_KEY_LEFT_CONTROL);
    c.movement.toggleProne = in.prone; in.prone = false;
    c.reload = in.reload; in.reload = false; c.drag = in.drag; in.drag = {};
    c.signal = in.pendingSignal; in.pendingSignal = multiplayer::SignalKind::None;
    if (!in.wheelOpen && !in.leftHeld && !in.rightHeld) in.signalMouseSuppressed = false;
    c.left = !in.wheelOpen && !in.signalMouseSuppressed && !in.actionSelection.open() && !in.suppressLeftUntilRelease && !in.weaponSwitching && !in.weaponReloading && in.slot == in.equippedSlot &&
        (in.leftPressed || in.leftHeld);
    c.right = !in.wheelOpen && !in.signalMouseSuppressed && c.interaction &&
        (in.rightPressed || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    in.leftPressed = in.rightPressed = false;
#endif
    return c;
}
}
int multiplayer::runClientSession(const Options& options, GLFWwindow* window,
                                  const SessionFeedback& feedback, const std::function<void()>& checkServer,
                                  const std::function<std::optional<double>()>& serverSimulationMilliseconds) {
#ifndef MP_GRAPHICS
    (void)serverSimulationMilliseconds;
#endif
    std::unique_ptr<Scene> loaded;
    if (feedback) {
        auto loading = std::async(std::launch::async, [&] {
            return std::make_unique<Scene>(options, ZombiePopulationLoader{}, std::nullopt, SceneRole::Replica);
        });
        bool cancelled = false;
        while (loading.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            if (!feedback(cancelled ? "Cancelling - finishing scene preparation..." : "Loading scene...")) cancelled = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        loaded = loading.get();
        if (cancelled) return 0;
    } else loaded = std::make_unique<Scene>(options, ZombiePopulationLoader{}, std::nullopt, SceneRole::Replica);
    auto& scene = *loaded;
    for (auto& instance : scene.zombies.instances) instance.removed = true;
    multiplayer::Prediction prediction;
    multiplayer::Snapshots snapshots;
    viewer::boid::Replica birds;
    bool birdCheckpointRequested=false;
    std::size_t receivedBirdFrames=0;
    Input in; in.camera.setPosition(scene.spawnEye); in.camera.setOrientation(options.look.x, options.look.y);
    std::optional<viewer::PlayerInventory> inventory;
    Graphics graphics;
    if (!options.headless) {
        inventory.emplace(viewer::loadPlayerInventory(multiplayer::resourceRoot()));
        graphics.initialize(scene, in, options, window, *inventory);
    }
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
#ifdef MP_GRAPHICS
    double lastDamageSeconds = -1;
    auto titleTime = start;
    std::size_t intervalFrames = 0;
    double intervalRenderSeconds = 0, intervalSimulationSeconds = 0;
#endif
    std::uint64_t lastInventoryCommand = 0;
    auto weaponDefinitions = scene.initialWeapons;
    while (!graphics.closed() && !in.leave) {
        if (checkServer) checkServer();
        const auto clockNow = Clock::now();
        const double now = std::chrono::duration<double>(clockNow - start).count();
        if (options.seconds && now >= options.seconds) break;
        const double dt = std::min(.1, std::chrono::duration<double>(clockNow - previous).count()); previous = clockNow;
#ifdef MP_GRAPHICS
        if (graphics.window) glfwPollEvents();
        if (in.hud) {
            auto actions = in.hud->takeInventoryActions();
            in.inventoryActions.insert(in.inventoryActions.end(), actions.begin(), actions.end());
        }
#endif
        if (!prediction.ready() && feedback) {
            if (!feedback("Connecting - waiting for player state...")) break;
            graphics.bindInput(in);
        }
#ifdef MP_GRAPHICS
        const auto simulationStart = Clock::now();
        bool newAudioSnapshot = false;
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
#ifdef MP_GRAPHICS
                newAudioSnapshot = true;
#endif
                lastSnapshot = now; ++received; mostPlayers = std::max(mostPlayers, snapshot->players.size());
                for (const auto& p : snapshot->players) if (p.id != self) {
                    const auto origin = remoteOrigins.emplace(p.id, p.movement.feet).first->second;
                    remoteTravel = std::max(remoteTravel, glm::length(p.movement.feet - origin));
                }
                snapshots.apply(scene, now);
                bool found = false;
                for (const auto& p : snapshot->players) if (p.id == self) {
                    const bool ready = prediction.ready(); const auto before = prediction.eye();
#ifdef MP_GRAPHICS
                    // Only accepted, newer snapshots reach here. The first one
                    // establishes a baseline; repeats cannot restart the flash.
                    if (ready && p.health < ownState.health) lastDamageSeconds = now;
#endif
                    if (!ready || p.slot != ownState.slot || p.vehicle != ownState.vehicle || p.seat != ownState.seat) {
                        if (!weaponDefinitions.equip(p.slot))
                            throw std::runtime_error("Server equipped an unavailable weapon");
                        while (in.camera.viewMode() != viewer::Camera::ViewMode::original)
                            in.camera.cycleViewMode();
                    }
                    in.canScope = p.vehicle == multiplayer::noVehicle && weaponDefinitions.equipped().config.hasScope;
                    in.equippedSlot = p.slot;
                    in.weaponSwitching = p.weaponSwitchRemaining > 0;
                    in.weaponReloading = p.reloadRemaining > 0;
                    // Commands mask firing while busy. A synthetic false is
                    // not a physical release and must not rearm a held click.
                    if ((in.weaponSwitching || in.weaponReloading) && in.leftHeld)
                        in.suppressLeftUntilRelease = true;
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
                    if (in.driver.occupied()) in.cancelSignal();
                    ownState=p; found=true;
                    bool inventoryIdle = !p.inventoryDragging && in.inventoryActions.empty() && p.resolved >= lastInventoryCommand;
#ifdef MP_GRAPHICS
                    inventoryIdle = inventoryIdle && (!in.hud || !in.hud->inventoryDragging());
#endif
                    if (inventory && inventoryIdle && !p.inventoryPlacements.empty()) {
                        std::map<std::string, viewer::ItemPlacement> placements;
                        std::size_t index = 0;
                        bool changed = false;
                        for (const auto& [id, item] : inventory->items()) {
                            (void)item;
                            if (index >= p.inventoryPlacements.size()) throw std::runtime_error("Incomplete authoritative inventory placements");
                            const auto& placement = p.inventoryPlacements[index++];
                            const auto& current = inventory->placements().at(id);
                            changed |= current.storage != placement.storage || current.x != placement.x ||
                                current.y != placement.y || current.rotation != placement.rotation;
                            placements.emplace(id, placement);
                        }
                        if (index != p.inventoryPlacements.size() || !inventory->synchronizePlacements(placements))
                            throw std::runtime_error("Invalid authoritative inventory placements");
#ifdef MP_GRAPHICS
                        if (changed && in.hud) in.hud->refreshInventoryLayout();
#else
                        (void)changed;
#endif
                    }
                    if (inventory && !p.ammoQuantities.empty()) {
                        std::size_t stack = 0;
                        for (const auto& [id, item] : inventory->items()) {
                            if (!item.inventory().stackable) continue;
                            if (stack >= p.ammoQuantities.size() || p.ammoQuantities[stack] > static_cast<std::uint32_t>(item.inventory().maxStack) ||
                                !inventory->setQuantity(id, static_cast<int>(p.ammoQuantities[stack++])))
                                throw std::runtime_error("Invalid authoritative ammo stack quantities");
                        }
                        if (stack != p.ammoQuantities.size()) throw std::runtime_error("Unexpected authoritative ammo stacks");
                    }
                    if (!ready) {
                        in.slot = p.slot;
                        if (!options.explicitLook) in.camera.setOrientation(p.yaw, p.pitch);
                    }
                    const auto correction = before - prediction.eye();
                    if (ready && walking && glm::length(correction) < 2) visualOffset += correction; else visualOffset = {};
                }
                if (!found) throw std::runtime_error("Server omitted local player state");
            } else if (const auto* setups = std::get_if<multiplayer::ZombieSetups>(&message)) {
                // Bulk setup delivery can precede Welcome on the other lane.
                // The loaded scene is sufficient to validate and retain it.
                snapshots.pushSetups(scene, *setups);
                multiplayer::ZombieSetupAck ack;
                for (const auto& setup : setups->zombies) ack.zombies.push_back({setup.id, setup.version});
                if (!network.send(connection, ack, true)) throw std::runtime_error("Cannot acknowledge zombie setup");
            } else if (const auto* pose = std::get_if<multiplayer::CorpsePose>(&message)) {
                snapshots.pushPose(scene, *pose, now);
                if (!network.send(connection, multiplayer::PoseAck{pose->id, pose->version}, true))
                    throw std::runtime_error("Cannot acknowledge corpse pose");
            } else if (const auto* frame=std::get_if<multiplayer::BirdFrame>(&message)) {
                if (!self) throw std::runtime_error("Bird frame before welcome");
                if (birds.receive(*frame)) {
                    ++receivedBirdFrames;
                    if (frame->hasCheckpoint) birdCheckpointRequested=false;
                } else if (!birdCheckpointRequested) {
                    if (!network.send(connection,multiplayer::BirdResync{},true)) throw std::runtime_error("Cannot request boid checkpoint");
                    birdCheckpointRequested=true;
                }
            } else throw std::runtime_error("Unexpected server message");
        }
#ifdef MP_GRAPHICS
        // Consume the newest accepted state once: a receive backlog must not
        // replay a burst of already stale audio from intermediate snapshots.
        if (newAudioSnapshot && graphics.audio) graphics.audio->receive(snapshots.latest(), self);
#endif
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
                const auto predicted = prediction.advance(nextCommand, *scene.collision);
                if (!predicted.inventoryActions.empty()) lastInventoryCommand = predicted.sequence;
#ifdef MP_GRAPHICS
                const auto movement = controller.feet() - shrubBefore.feet;
                graphics.speedMetersPerSecond = glm::length(glm::vec2(movement.x, movement.z)) / multiplayer::tickSeconds;
                // Fresh prediction only: reconciliation replays must stay silent.
                if (graphics.audio) {
                    const bool running = nextCommand.movement.run && controller.stance() == viewer::PlayerStance::Standing;
                    graphics.audio->weaponCommand(predicted, now, weaponDefinitions, *inventory);
                    if (!in.driver.occupied()) {
                        graphics.audio->localMovement(controller, shrubBefore, scene.undergrowth, running, self);
                        auto soundCamera = in.camera;
                        soundCamera.setPosition(controller.eyePosition());
                        graphics.audio->localDoor(predicted, soundCamera, scene);
                    }
                }
#endif
                auto outgoing = prediction.commandsToSend(); outgoing.snapshotTick = snapshots.latest().tick;
                multiplayer::ReplicationView view;
                view.eye = prediction.eye();
                view.yaw = nextCommand.movement.yawDegrees;
                view.pitch = nextCommand.pitch;
#ifdef MP_GRAPHICS
                if (graphics.window) {
                    const auto camera = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, now,
                        !in.driver.occupied() && !in.interaction);
                    int viewWidth = 0, viewHeight = 0;
                    glfwGetFramebufferSize(graphics.window, &viewWidth, &viewHeight);
                    if (viewWidth > 0 && viewHeight > 0) {
                        const auto renderSize = options.display.renderSize({viewWidth, viewHeight});
                        view.aspect = std::clamp(float(renderSize.x) / float(renderSize.y), .2f, 8.f);
                        view.verticalFov = glm::degrees(2.f * std::atan(1.f / camera.projection(view.aspect)[1][1]));
                    }
                    view.eye = camera.position();
                    view.yaw = camera.yawDegrees();
                    view.pitch = camera.pitchDegrees();
                }
#endif
                outgoing.view = view;
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
            graphics.audio->update(in.camera, scene, snapshots, self, static_cast<float>(dt), now);
            int width = 0, height = 0; glfwGetFramebufferSize(graphics.window, &width, &height);
            if (width > 0 && height > 0) {
                const auto renderSize = options.display.renderSize({width, height});
                const int renderWidth = renderSize.x, renderHeight = renderSize.y;
                const float renderAspect = float(renderWidth) / renderHeight;
                intervalSimulationSeconds += std::chrono::duration<double>(Clock::now() - simulationStart).count();
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
                renderer.resize(renderWidth, renderHeight); renderer.updateDoors(*scene.doors); renderer.updateBoxes(*scene.boxes);
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
                graphics.gpuTimer->begin();
                const auto renderStart = Clock::now();
                renderer.render(viewCamera, snapshots.animationSeconds(), [&] {
                    graphics.birds->draw(viewCamera,renderAspect,snapshots.animationSeconds(),birds.sample(snapshots.animationSeconds()));
                    graphics.players->draw(viewCamera, snapshots.players(), self, renderWidth, renderHeight);
                    graphics.body->drawPose(ownBody,viewCamera.projection(renderAspect)*viewCamera.view(),renderWidth,renderHeight);
                });
                if (prediction.ready() && !in.driver.occupied() && !in.interaction &&
                    in.camera.viewMode() == viewer::Camera::ViewMode::original &&
                    !in.weaponSwitching && viewer::weaponViewVisible(weaponDefinitions.equipped().config, ownState.loaded, ownState.weaponCoolingDown))
                    graphics.weapon->draw(ownState.slot, renderWidth, renderHeight, flash,
                                          in.weaponMotion.pose(ownState.slot, now));
                const auto overlayViewport = renderer.beginOverlay(width, height);
                if (!in.driver.occupied()) graphics.reticle->draw(in.camera.viewMode(), overlayViewport.width, overlayViewport.height);
                viewer::HudState hudState;
                hudState.seconds = now;
                hudState.screen = in.hudScreen;
                hudState.position = in.driver.occupied() ? in.driver.eyePosition(*scene.vehicles) : in.camera.position();
                hudState.yawDegrees = in.camera.yawDegrees();
                hudState.showLocalMapPlayer = prediction.ready();
                if (in.hudScreen == viewer::HudScreen::Map) {
                    for (const auto& player : snapshots.players()) {
                        if (player.id == self) continue;
                        const auto position = player.movement.feet;
                        hudState.mapPlayers.push_back({{position.x, position.z}, player.yaw,
                            "PLAYER " + std::to_string(player.id), false});
                    }
                }
                const auto stance = prediction.controller().stance();
                hudState.movement = in.driver.occupied() ? (in.driver.driving() ? "DRIVING" : "PASSENGER") :
                    stance == viewer::PlayerStance::Prone ? "PRONE" : stance == viewer::PlayerStance::Crouched ? "CROUCH" : "STAND";
                hudState.mode = in.interaction ? "INTERACT" : "COMBAT";
                if (const auto car = in.driver.car()) {
                    const auto velocity = scene.vehicles->velocity(*car);
                    hudState.speedMetersPerSecond = glm::length(glm::vec2(velocity.x, velocity.z));
                } else hudState.speedMetersPerSecond = graphics.speedMetersPerSecond;
                hudState.mouseCaptured = in.capture;
                hudState.selectionSlot = in.actionSelection.selected();
                hudState.commandWheelOpen = in.wheelOpen;
                hudState.commandWheelOffset = in.wheelOffset;
                if (!snapshots.empty()) {
                    const float scale = std::min(float(width) / 1280, float(height) / 720);
                    const auto matrix = viewCamera.projection(renderAspect) * viewCamera.view();
                    for (const auto& signal : snapshots.latest().signals) {
                        const auto placement = multiplayer::signalPlacement(viewCamera, signal.position + glm::vec3(0, 1, 0),
                            overlayViewport.width, overlayViewport.height, 56 * scale);
                        const auto clip = matrix * glm::vec4(placement.position, 1);
                        const glm::vec2 point{clip.x / clip.w * .5f + .5f, .5f - clip.y / clip.w * .5f};
                        hudState.signals.push_back({signal.kind,
                            {(overlayViewport.x + point.x * overlayViewport.width) / width,
                             (overlayViewport.y + point.y * overlayViewport.height) / height}, placement.atEdge});
                    }
                }
                hudState.weaponSwitchRemaining = ownState.weaponSwitchRemaining;
                hudState.weaponSwitchDuration = ownState.weaponSwitchDuration;
                hudState.reloadRemaining = ownState.reloadRemaining;
                hudState.reloadDuration = ownState.reloadDuration;
                if (prediction.ready()) {
                    hudState.health = ownState.health;
                    hudState.lastDamageSeconds = lastDamageSeconds;
                    hudState.equipment = weaponDefinitions.equipped().config.name;
                    hudState.slot = ownState.slot;
                    if (ownState.slot != viewer::punchSlot) {
                        hudState.loaded = static_cast<int>(ownState.loaded);
                        hudState.reserve = static_cast<int>(ownState.reserve);
                    }
                }
                if (in.driver.occupied()) hudState.prompt = "Enter: exit vehicle";
                else if (birds.ready() && ownState.health>0)
                    for (const auto& flock:birds.simulation().state().flocks)
                        if (flock.control.mode==viewer::boid::Mode::gathering || flock.control.mode==viewer::boid::Mode::swooping) {
                            hudState.prompt="Birds diving! Move aside, duck or reach cover"; break;
                        }
                graphics.hud->draw(hudState, width, height);
                intervalRenderSeconds += std::chrono::duration<double>(Clock::now() - renderStart).count();
                graphics.gpuTimer->end();
                // Presentation and VSync waits stay outside the render timers.
                renderer.present(width, height); glfwSwapBuffers(graphics.window);
                viewer::Renderer::checkErrors("multiplayer frame");
                if (!intervalFrames) titleTime = clockNow;
                ++intervalFrames;
                const double intervalSeconds = std::chrono::duration<double>(Clock::now() - titleTime).count();
                if (intervalSeconds >= 1.0) {
                    const auto fps = static_cast<int>(static_cast<double>(intervalFrames) / intervalSeconds);
                    const auto milliseconds = [](double value) {
                        std::ostringstream text;
                        text << std::fixed << std::setprecision(2) << value << " ms";
                        return text.str();
                    };
                    const auto gpuMilliseconds = graphics.gpuTimer->takeAverageMilliseconds();
                    const auto gpuTime = gpuMilliseconds ? milliseconds(*gpuMilliseconds) :
                        graphics.gpuTimer->supported() ? std::string("pending") : std::string("unavailable");
                    const double frameScale = 1000.0 / static_cast<double>(intervalFrames);
                    const auto serverMilliseconds = serverSimulationMilliseconds ?
                        serverSimulationMilliseconds() : std::nullopt;
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
                        + (entry ? " | " + viewer::vehicleEntryLabel(*scene.vehicles, *entry) : "")
                        + " | " + std::to_string(fps) + " FPS | GPU " + gpuTime
                        + " | CPU render " + milliseconds(intervalRenderSeconds * frameScale)
                        + " | CPU sim " + milliseconds(intervalSimulationSeconds * frameScale)
                        + (serverSimulationMilliseconds ? " | Server sim " +
                            (serverMilliseconds ? milliseconds(*serverMilliseconds) + "/tick" : "pending") : "");
                    glfwSetWindowTitle(graphics.window, title.c_str());
                    intervalFrames = 0;
                    intervalRenderSeconds = intervalSimulationSeconds = 0;
                }
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
        << " feet=" << feet.x << ',' << feet.y << ',' << feet.z << " remote_travel=" << remoteTravel
        << " bird_frames="<<receivedBirdFrames<<" bird_tick="<<birds.simulation().state().tick
        << " zombies=" << snapshots.trackedZombieIds().size() << '\n';
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
