#include "session.hpp"
#include <utility>
#include "session_setup.hpp"
#include "inventory_state.hpp"
#include "network.hpp"
#include "character_appearances.hpp"
#include "character_skin.hpp"
#include "character_profile.hpp"
#include "sleep_watch.hpp"
#include "resources.hpp"
#include "prediction.hpp"
#include "snapshots.hpp"
#include "firearm_hits.hpp"
#include "signals.hpp"
#include "boid/replica.hpp"
#include "camera.hpp"
#include "weapon_motion.hpp"
#include "weapon_accuracy.hpp"
#include "action_selection.hpp"

#include "vehicle_player.hpp"
#include "inventory/inventory_config.hpp"
#ifdef MP_GRAPHICS
#include "players.hpp"
#include "flashlights.hpp"
#include "renderer.hpp"
#include "gpu_timer.hpp"
#include "boid/bird_renderer.hpp"
#include "reticle_overlay.hpp"
#include "gui/hud.hpp"
#include "minimap.hpp"
#include "weapon_overlay.hpp"
#include "player_body_renderer.hpp"
#include "audio_playback.hpp"
#include "weapon_feedback.hpp"
#include "sound_cues.hpp"
#include "window.hpp"
#include "mouse_input.hpp"
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
    viewer::ShotInaccuracy shotInaccuracy;
    viewer::ActionSelection actionSelection;
    viewer::ActionShortcuts actions;
    bool suppressLeftUntilRelease = false, suppressRightUntilRelease = false, weaponSwitching = false, weaponReloading = false;
    std::size_t equippedSlot = 0; // Authoritative slot, independent of requested slot.

    viewer::VehiclePlayer driver;
    bool enterVehicle = false, toggleDrivingCamera = false;
    bool capture = true, firstMouse = true, prone = false, reload = false;
    bool equipmentCursor = false, equipmentGripHeld = false;
    bool leftPressed = false, rightPressed = false, leftHeld = false;
    bool rightHeld = false, wheelOpen = false, signalMouseSuppressed = false;
    bool cancelInteraction = false; // Preserve cancellation even between outgoing commands.
    std::array<bool, 2> altHeld{};
    glm::vec2 wheelOffset{0}; // Design dp; same scale as the shared HUD.
    multiplayer::SignalKind pendingSignal = multiplayer::SignalKind::None;
    void cancelSignal() { wheelOpen = false; pendingSignal = multiplayer::SignalKind::None; }
    bool canScope = false;
    bool canThrowGrenade = false;
    bool flashlight = true;
    bool leave = false;
    std::vector<viewer::InventoryDragAction> inventoryActions;
    std::optional<viewer::SleepAction> sleepAction;
    bool inSleepSession = false; // Sleeping or standing automatic watch; both suspend direct control.
    std::uint64_t sleepRevision = 0;
#ifdef MP_GRAPHICS
    bool distantSceneCache = false;
    viewer::HudScreen hudScreen = viewer::HudScreen::Gameplay;
    viewer::Hud* hud = nullptr; // Borrowed from Graphics on the GL thread.
#endif
    double lastX = 0, lastY = 0;
    glm::vec2 drag{0};
    std::uint8_t slot = 0;
    bool temporaryHandling() const {
        return (capture || equipmentCursor) && !driver.occupied() && equippedSlot != viewer::handlingSlot && (altHeld[0] || altHeld[1]);
    }
    bool handling() const { return equippedSlot == viewer::handlingSlot || temporaryHandling(); }
};
#ifdef MP_GRAPHICS
Input& input(GLFWwindow* window) { return *static_cast<Input*>(glfwGetWindowUserPointer(window)); }
void showHudScreen(GLFWwindow* window, viewer::HudScreen screen) {
    auto& in = input(window);
    in.cancelInteraction = true;
    in.equipmentCursor = false;
    in.altHeld.fill(false);
    in.hudScreen = screen;
    in.suppressLeftUntilRelease |= in.leftHeld;
    in.suppressRightUntilRelease |= in.rightHeld;
    in.firstMouse = true; in.drag = {};
    in.cancelSignal();
    in.actionSelection.close();
    in.leftPressed = in.rightPressed = in.prone = in.reload = in.enterVehicle = in.toggleDrivingCamera = false;
    if (in.hud) in.hud->cancelPointerInteraction();
    glfwSetInputMode(window, GLFW_CURSOR,
        in.capture && screen == viewer::HudScreen::Gameplay ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}
void key(GLFWwindow* window, int code, int, int action, int) {
    auto& in = input(window);
    if (in.inSleepSession && code != GLFW_KEY_TAB && code != GLFW_KEY_HOME) {
        if (code == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            in.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Cancel, in.sleepRevision, 30};
        return;
    }
    if (code == GLFW_KEY_LEFT_ALT || code == GLFW_KEY_RIGHT_ALT) {
        const bool wasTemporaryHandling = in.temporaryHandling();
        in.altHeld[code == GLFW_KEY_RIGHT_ALT ? 1 : 0] = action != GLFW_RELEASE &&
            in.hudScreen == viewer::HudScreen::Gameplay && (in.capture || in.equipmentCursor);
        if (wasTemporaryHandling != in.temporaryHandling()) {
            in.cancelInteraction = true;
            in.suppressLeftUntilRelease |= in.leftHeld;
            in.suppressRightUntilRelease |= in.rightHeld;
            in.leftPressed = in.rightPressed = in.reload = false;
            in.drag = {}; in.actionSelection.close();
            if (in.temporaryHandling()) while (in.camera.viewMode() != viewer::Camera::ViewMode::original)
                in.camera.cycleViewMode();
        }
        if (action == GLFW_PRESS && in.driver.occupied() && in.hudScreen == viewer::HudScreen::Gameplay)
            in.toggleDrivingCamera = true;
        return;
    }
    if (action != GLFW_PRESS) return;
    if ((code >= GLFW_KEY_F1 && code <= GLFW_KEY_F4) || code == GLFW_KEY_F12 ||
        (code == GLFW_KEY_ESCAPE && in.hudScreen != viewer::HudScreen::Gameplay)) {
        constexpr std::array<viewer::HudScreen, 4> screens{viewer::HudScreen::Inventory, viewer::HudScreen::Skills,
            viewer::HudScreen::Health, viewer::HudScreen::Map};
        const auto requested = code == GLFW_KEY_ESCAPE ? viewer::HudScreen::Gameplay :
            code == GLFW_KEY_F12 ? viewer::HudScreen::Graphics : screens[code - GLFW_KEY_F1];
        showHudScreen(window, in.hudScreen == requested ? viewer::HudScreen::Gameplay : requested);
        return;
    }
    if (in.hudScreen != viewer::HudScreen::Gameplay) return;
    if (code == GLFW_KEY_TAB) {
        in.equipmentCursor = in.capture; in.capture = !in.capture;
        if (in.equipmentCursor) in.equipmentGripHeld = in.handling() && in.leftHeld;
        if (!in.handling()) {
            in.cancelInteraction = true;
            in.suppressLeftUntilRelease |= in.leftHeld;
            in.suppressRightUntilRelease |= in.rightHeld;
        }
        in.leftPressed = in.rightPressed = in.reload = false;
        in.firstMouse = true; in.actionSelection.close();
        glfwSetInputMode(window, GLFW_CURSOR, in.capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        return;
    }
    if (code == GLFW_KEY_F7) {
        in.distantSceneCache = !in.distantSceneCache;
        std::cout << "Distant scenery cache: " << (in.distantSceneCache ? "on" : "off") << '\n';
        return;
    }
    if (code == GLFW_KEY_ESCAPE && in.wheelOpen) { in.cancelSignal(); return; }
    if (in.wheelOpen && code != GLFW_KEY_HOME) return;
    if (code == GLFW_KEY_ESCAPE && in.actionSelection.open()) { in.actionSelection.close(); return; }
    if (code == GLFW_KEY_ESCAPE) in.leave = true;
    if (code == GLFW_KEY_ENTER && in.capture) in.enterVehicle = true;
    if (in.driver.occupied() && code != GLFW_KEY_HOME) return;
    if (code == GLFW_KEY_C) in.prone = true;
    if (code == GLFW_KEY_R && !in.temporaryHandling() && !in.weaponSwitching && !in.weaponReloading && !in.actionSelection.open()) {
        in.reload = true;
        in.suppressLeftUntilRelease |= in.leftHeld;
    }
    if (code == GLFW_KEY_L) in.flashlight = !in.flashlight;
    if (code == GLFW_KEY_0 || code == GLFW_KEY_GRAVE_ACCENT || (code >= GLFW_KEY_1 && code <= GLFW_KEY_9)) {
        in.cancelInteraction = true;
        in.actionSelection.highlight((code == GLFW_KEY_0 || code == GLFW_KEY_GRAVE_ACCENT) ? 0 : code - GLFW_KEY_1 + 1);
        in.leftPressed = false;
    }
    if (code == GLFW_KEY_HOME) {
        in.equipmentCursor = false;
        in.altHeld.fill(false);
        in.cancelInteraction = true;
        in.cancelSignal();
        in.suppressLeftUntilRelease |= in.leftHeld;
        in.suppressRightUntilRelease |= in.rightHeld;
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
    if (in.inSleepSession && in.capture) { in.firstMouse = true; return; }
    if (in.hudScreen != viewer::HudScreen::Gameplay || !in.capture) {
        hudPointer(window, in.hud, x, y);
        if (in.equipmentCursor && !in.firstMouse)
            in.drag = glm::clamp(in.drag + glm::vec2(float(x - in.lastX), float(in.lastY - y)), glm::vec2(-2000), glm::vec2(2000));
        in.lastX = x; in.lastY = y; in.firstMouse = false; return;
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
        if (length > viewer::commandPageOuterRadius) in.wheelOffset *= viewer::commandPageOuterRadius / length;
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
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT) {
        in.rightHeld = action == GLFW_PRESS;
        if (action == GLFW_RELEASE) in.suppressRightUntilRelease = false;
        else if (!in.capture || in.driver.occupied() || in.wheelOpen || in.signalMouseSuppressed ||
                 in.hudScreen != viewer::HudScreen::Gameplay || in.actionSelection.open() ||
                 in.weaponSwitching || in.weaponReloading || in.slot != in.equippedSlot)
            in.suppressRightUntilRelease = true;
    }
    if (buttonCode == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS && in.capture && !in.driver.occupied() && in.hudScreen == viewer::HudScreen::Gameplay) {
            in.cancelInteraction = true;
            in.wheelOpen = true; in.wheelOffset = {}; in.drag = {};
            in.actionSelection.close();
            in.leftPressed = in.rightPressed = in.reload = false;
            in.signalMouseSuppressed = true;
        } else if (action == GLFW_RELEASE && in.wheelOpen) {
            if (const auto page = viewer::commandWheelPageSelection(in.wheelOffset)) showHudScreen(window, *page);
            else {
                in.pendingSignal = multiplayer::signalWheelSelection(in.wheelOffset);
                in.wheelOpen = false;
            }
        }
        return;
    }
    if (in.hudScreen != viewer::HudScreen::Gameplay || !in.capture) {
        double x, y; glfwGetCursorPos(window, &x, &y);
        hudPointer(window, in.hud, x, y);
        if (in.hud) in.hud->pointerButton(buttonCode, action == GLFW_PRESS);
        return;
    }
    if (buttonCode == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE && in.capture && in.handling())
        in.inventoryActions.push_back({viewer::InventoryDragActionKind::HandRelease, "", ""});
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
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && (in.handling() || in.canThrowGrenade)) in.rightPressed = true;
    if (buttonCode == GLFW_MOUSE_BUTTON_RIGHT && in.canScope && !in.handling()) in.camera.cycleViewMode();
}
void scroll(GLFWwindow* window, double, double y) {
    auto& in = input(window);
    if (in.wheelOpen) return;
    if ((in.hudScreen != viewer::HudScreen::Gameplay || !in.capture) && in.hud) {
        double x, cursorY; glfwGetCursorPos(window, &x, &cursorY);
        hudPointer(window, in.hud, x, cursorY);
        in.hud->mouseWheel(static_cast<float>(y));
    }
    if (in.hudScreen == viewer::HudScreen::Gameplay && in.capture && !in.driver.occupied() && std::isfinite(y) && y != 0) {
        in.cancelInteraction = true;
        in.actionSelection.cycle(y > 0 ? 1 : -1, in.actions, in.equippedSlot);
        in.leftPressed = false;
    }
}
void focus(GLFWwindow* window, int focused) {
    if (!focused) {
        auto& in = input(window); in.cancelInteraction = true; in.capture = false; in.equipmentCursor = false; in.firstMouse = true;
        in.altHeld.fill(false);
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
    std::unique_ptr<viewer::RuntimeGraphicsSettings> settings;
    std::unique_ptr<viewer::GpuTimer> gpuTimer;
    std::unique_ptr<viewer::BirdRenderer> birds;
    std::unique_ptr<viewer::ReticleOverlay> reticle;
    std::unique_ptr<viewer::Hud> hud;
    float speedMetersPerSecond = 0;
    std::unique_ptr<viewer::WeaponOverlay> weapon;
    std::unique_ptr<multiplayer::ClientAudio> audio;
    multiplayer::WeaponFeedbackPrediction weaponFeedback;
    multiplayer::RemoteSoundCues remoteEvents;
    viewer::BloodMist localBloodMist;
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
        const auto renderSize = options.display.renderSize();
        std::cout << (options.display.fullscreen ? "Fullscreen" : "Windowed") << "; render resolution "
                  << renderSize.x << 'x' << renderSize.y << "; GUI resolution " << width << 'x' << height << '\n';
        bindInput(in);
        if (!options.hidden) viewer::enableRawMouseMotion(window);
        const auto shaders = multiplayer::resourceRoot() / "shaders";
        renderer = std::make_unique<viewer::Renderer>(scene.model, shaders, scene.trees ? &*scene.trees : nullptr,
            &scene.zombies, scene.forests ? &*scene.forests : nullptr, &scene.dogs);
        in.distantSceneCache = options.distantSceneCache;
        renderer->setDistantSceneCache(in.distantSceneCache);
        renderer->setDistantSceneDistance(viewer::distantSceneryDistances[options.graphics.distantScenery]);
        renderer->setShadowCasters(options.graphics.shadowCasters);
        renderer->setSkipHighestDetail(options.graphics.skipHighestDetail);
        settings = std::make_unique<viewer::RuntimeGraphicsSettings>(window, *renderer, options.display.fullscreen,
            options.display.renderSize(), options.graphicsSettingsFile);
        scene.zombies.releaseAnimationVertices();
        scene.dogs.releaseAnimationVertices();
        renderer->setGroundItems(*scene.groundItems, scene.groundModels);
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
        renderer->loadGrenadeModel(multiplayer::weaponModelDirectory() / "molotov.glb", viewer::GrenadeKind::molotov);
        players = std::make_unique<multiplayer::PlayerRenderer>(weapon.get());
        body = std::make_unique<viewer::PlayerBodyRenderer>(shaders);
        birds = std::make_unique<viewer::BirdRenderer>(shaders, multiplayer::resourceRoot() / "assets/pigeon/flock.json");
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
multiplayer::Command command(Input& in, GLFWwindow* window, bool bot, double seconds, float swayScale = 1) {
    multiplayer::Command c;
    c.inventoryActions.swap(in.inventoryActions);
    c.sleepAction = std::exchange(in.sleepAction, std::nullopt);
    const auto aim = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, seconds, !in.driver.occupied() && !in.handling(), swayScale);
    c.movement.yawDegrees = aim.yawDegrees(); c.pitch = aim.pitchDegrees(); c.slot = in.slot;
    c.objectHandling = in.temporaryHandling();
    c.flashlight = in.flashlight;
    if (in.inSleepSession) { c.cancelInteraction = true; return c; }
    if (bot) { c.movement.forward = 1; c.movement.yawDegrees = 0; c.pitch = 0; return c; }
    c.cancelInteraction = true;
    if (!window) return c;
    if (!in.capture) {
        if (in.equipmentCursor) {
            c.cancelInteraction = in.cancelInteraction; in.cancelInteraction = false;
            // Preserve a pre-existing world grip until the drawer has routed
            // its release to storage or ground on the rendering thread.
            c.left = in.equipmentGripHeld; c.drag = in.drag; in.drag = {};
        }
        return c;
    }
#ifdef MP_GRAPHICS
    if (in.hudScreen != viewer::HudScreen::Gameplay) return c;
    c.cancelInteraction = in.cancelInteraction || in.wheelOpen || in.signalMouseSuppressed || in.actionSelection.open();
    in.cancelInteraction = false;
    c.enterVehicle = in.enterVehicle; in.enterVehicle = false;
    const auto held = [&](int code) { return glfwGetKey(window, code) == GLFW_PRESS ? 1.0f : 0.0f; };
    c.movement.forward = held(GLFW_KEY_W) - held(GLFW_KEY_S); c.movement.right = held(GLFW_KEY_D) - held(GLFW_KEY_A);
    c.movement.run = held(GLFW_KEY_LEFT_SHIFT); c.movement.jump = held(GLFW_KEY_SPACE); c.movement.crouch = held(GLFW_KEY_LEFT_CONTROL);
    c.movement.toggleProne = in.prone; in.prone = false;
    c.reload = in.reload; in.reload = false; c.drag = in.drag; in.drag = {};
    c.signal = in.pendingSignal; in.pendingSignal = multiplayer::SignalKind::None;
    if (!in.wheelOpen && !in.leftHeld && !in.rightHeld) in.signalMouseSuppressed = false;
    c.left = !in.driver.occupied() && !c.cancelInteraction && !in.wheelOpen && !in.signalMouseSuppressed && !in.actionSelection.open() && !in.suppressLeftUntilRelease && !in.weaponSwitching && !in.weaponReloading && in.slot == in.equippedSlot &&
        (in.handling() ? in.leftHeld : (in.leftPressed || in.leftHeld));
    c.right = !in.driver.occupied() && !c.cancelInteraction && !in.suppressRightUntilRelease && !in.wheelOpen && !in.signalMouseSuppressed && !in.actionSelection.open() &&
        !in.weaponSwitching && !in.weaponReloading && in.slot == in.equippedSlot && (in.handling() || in.canThrowGrenade) &&
        (in.rightPressed || in.rightHeld);
    in.leftPressed = in.rightPressed = false;
#endif
    return c;
}
}
int multiplayer::runClientSession(const Options& requestedOptions, GLFWwindow* window,
                                  const SessionFeedback& feedback, const std::function<void()>& checkServer,
                                  const std::function<std::optional<double>()>& serverSimulationMilliseconds) {
#ifndef MP_GRAPHICS
    (void)serverSimulationMilliseconds;
#endif
    const auto configured = discoverSession(requestedOptions, feedback, checkServer);
    if (!configured) return 0;
    const auto& options = *configured;
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
    prediction.setDayLengthSeconds(scene.dayNight.dayLengthSeconds);
    multiplayer::Snapshots snapshots;
    viewer::boid::Replica birds;
    bool birdCheckpointRequested=false;
    std::size_t receivedBirdFrames=0;
    Input in; in.camera.setPosition(scene.spawnEye); in.camera.setOrientation(options.look.x, options.look.y);
    std::optional<viewer::PlayerInventory> inventory;
    Graphics graphics;
    float playerSpeed = 0;
    const auto swayScale = [&] {
        const auto& controller = prediction.controller();
        return viewer::sniperSwayScale(playerSpeed, controller.movementState().posture, controller.config());
    };
    if (!options.headless) {
        inventory.emplace(viewer::loadPlayerInventory(multiplayer::resourceRoot()));
        graphics.initialize(scene, in, options, window, *inventory);
    }
    multiplayer::Network network(options.fakeLagMs, options.fakeLossPercent);
    multiplayer::CharacterAppearanceOutbox appearanceUpload;
    multiplayer::ClientCharacterAppearances appearances;
    auto localAppearance = multiplayer::defaultCharacterAppearance();
    std::string characterName;
    try {
        localAppearance = multiplayer::loadSavedCharacterAppearance(multiplayer::resourceRoot(), multiplayer::characterProfilePath(), &characterName);
    } catch (const std::exception& error) {
        std::cerr << "Character appearance: " << error.what() << "; using the default character\n";
    }
    const auto localAppearanceBytes = std::make_shared<const std::vector<std::uint8_t>>(multiplayer::encodeCharacterAppearance(localAppearance));
    const auto connection = network.connect(options.address);
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now(); auto previous = start;
    double accumulator = 0, lastSnapshot = 0;
    std::uint32_t self = 0;
    double facedGuardAlertAt = -1;
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
    auto inventoryWeapons = scene.initialWeapons;
#ifdef MP_GRAPHICS
    const auto hipFireSpread = [&] {
        const auto& controller = prediction.controller();
        return viewer::hipFireInaccuracy(weaponDefinitions.equipped().config, playerSpeed,
            controller.movementState().posture, controller.config(),
            in.camera.viewMode() != viewer::Camera::ViewMode::original);
    };
    if (in.hud) in.hud->setGroundItems(*scene.groundItems, inventoryWeapons, *scene.collision);
#endif
    while (!graphics.closed() && !in.leave) {
        if (checkServer) checkServer();
        const auto clockNow = Clock::now();
        const double now = std::chrono::duration<double>(clockNow - start).count();
        if (options.seconds && now >= options.seconds) break;
        const double dt = std::min(.1, std::chrono::duration<double>(clockNow - previous).count()); previous = clockNow;
#ifdef MP_GRAPHICS
        if (graphics.window) {
            if (const auto changed = graphics.hud->takeGraphicsSettingsRequest()) {
                graphics.settings->apply(*changed);
                in.distantSceneCache = changed->distantScenery != 3;
                in.firstMouse = true;
            }
            glfwPollEvents();
            graphics.settings->syncDistantScenery(in.distantSceneCache);
            graphics.hud->setGraphicsSettings(graphics.settings->settings());
        }
        if (in.hud) {
            if (const auto action = in.hud->takeSleepAction()) in.sleepAction = *action;
            auto actions = in.hud->takeInventoryActions();
            for (const auto& action : actions)
                if (action.kind == viewer::InventoryDragActionKind::HandStore || action.kind == viewer::InventoryDragActionKind::HandRelease)
                    in.equipmentGripHeld = false;
            in.inventoryActions.insert(in.inventoryActions.end(), actions.begin(), actions.end());
        }
#endif
        if (!prediction.ready() && feedback) {
            if (!feedback("Connecting - waiting for player state...")) break;
            graphics.bindInput(in);
        }
#ifdef MP_GRAPHICS
        const auto simulationStart = Clock::now();
#endif
        const auto messages = network.receive();
        for (const auto& event : network.events) {
            if (event.state == k_ESteamNetworkingConnectionState_Connected) {
                if (!network.send(connection, multiplayer::Hello{scene.fingerprint, characterName}, true)) throw std::runtime_error("Cannot send handshake");
            } else if (event.state == k_ESteamNetworkingConnectionState_ClosedByPeer || event.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
                throw std::runtime_error("Connection ended: " + event.reason);
        }
        for (const auto& [c, message] : messages) {
            (void)c;
            if (const auto* welcome = std::get_if<multiplayer::Welcome>(&message)) {
                if (self) throw std::runtime_error("Duplicate welcome");
                self = welcome->player;
                appearanceUpload.push(0, localAppearanceBytes);
                std::cout << "Connected as player " << self << std::endl;
            } else if (const auto* appearance = std::get_if<multiplayer::CharacterAppearanceChunk>(&message)) {
                const bool complete = appearances.receive(*appearance);
#ifdef MP_GRAPHICS
                if (graphics.players) {
                    if (!appearance->totalBytes) graphics.players->removeAppearance(appearance->player);
                    else if (complete) graphics.players->setAppearance(appearance->player, appearances.get(appearance->player));
                }
#endif
                if (complete) std::cout << "Character appearance received for player " << appearance->player << std::endl;
            } else if (const auto* snapshot = std::get_if<multiplayer::Snapshot>(&message)) {
                if (!self || !snapshots.push(*snapshot, now, network.estimatedOneWaySeconds(connection))) continue;
                lastSnapshot = now; ++received; mostPlayers = std::max(mostPlayers, snapshot->players.size());
                const bool wasResting = in.inSleepSession;
                in.inSleepSession = snapshot->sleepPlan.inSleepSession(self);
                in.sleepRevision = snapshot->sleepPlan.revision;
#ifdef MP_GRAPHICS
                if (in.inSleepSession && !wasResting) {
                    in.equipmentGripHeld = false; in.cancelInteraction = true; in.drag = {};
                    in.suppressLeftUntilRelease |= in.leftHeld; in.suppressRightUntilRelease |= in.rightHeld;
                    in.firstMouse = true;
                }
                if (in.inSleepSession) { in.hudScreen = viewer::HudScreen::Gameplay; in.cancelSignal(); in.actionSelection.close(); in.altHeld.fill(false); }
#else
                (void)wasResting;
#endif
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
                    in.canThrowGrenade = p.vehicle == multiplayer::noVehicle && weaponDefinitions.equipped().config.grenade.has_value();
                    in.equippedSlot = p.slot;
                    in.weaponSwitching = p.weaponSwitchRemaining > 0;
                    in.weaponReloading = p.reloadRemaining > 0;
                    // Commands mask firing while busy. A synthetic false is
                    // not a physical release and must not rearm a held click.
                    if ((in.weaponSwitching || in.weaponReloading) && in.leftHeld)
                        in.suppressLeftUntilRelease = true;
                    if ((in.weaponSwitching || in.weaponReloading) && in.rightHeld)
                        in.suppressRightUntilRelease = true;
#ifdef MP_GRAPHICS
                    graphics.weaponFeedback.reconcile(p);
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
                    if (inventory && inventoryIdle && p.inventoryState) {
                        bool layoutChanged = false;
                        if (!multiplayer::applyInventory(*inventory, scene.initialInventory, *p.inventoryState, &layoutChanged))
                            throw std::runtime_error("Invalid authoritative inventory");
                        multiplayer::applyGround(*scene.groundItems, scene.initialInventory, snapshot->groundItems);
                        if (in.slot < viewer::weaponIds.size() && !inventory->owns(viewer::weaponIds[in.slot])) in.slot = p.slot;
                        inventoryWeapons.synchronizeOwnership(*inventory, p.slot, static_cast<int>(p.loaded));
#ifdef MP_GRAPHICS
                        in.actions = viewer::actionShortcuts(*inventory);
                        if (layoutChanged && in.hud) in.hud->refreshInventoryLayout();
#endif
                    } else if (inventory && !p.ammoQuantities.empty()) {
                        // IDs keep live quantities attached to the correct
                        // stacks while ownership changes await acknowledgement.
                        if (p.ammoItemIds.size() != p.ammoQuantities.size()) throw std::runtime_error("Invalid authoritative ammo IDs");
                        for (std::size_t index=0; index<p.ammoItemIds.size(); ++index) {
                            const auto& itemId=p.ammoItemIds[index];
                            if (inventory->owns(itemId) && !inventory->setQuantity(itemId,static_cast<int>(p.ammoQuantities[index])))
                                throw std::runtime_error("Invalid authoritative ammo quantity");
                        }
                    }
                    if (!ready) {
                        in.slot = p.slot;
                        if (!options.explicitLook) in.camera.setOrientation(p.yaw, p.pitch);
                        std::cout << "Spawned as player " << self << std::endl;
                    }
                    const auto& sleep = snapshot->sleepPlan;
                    if (sleep.wakeReason == viewer::SleepWakeReason::GuardSpotted && sleep.guardAlert.guard == self &&
                        facedGuardAlertAt != sleep.endedAt) {
                        const auto angles = viewer::sleepGuardLookAngles(prediction.eye(), sleep.guardAlert.target);
                        in.camera.setOrientation(angles.x, angles.y);
                        in.firstMouse = true;
                        facedGuardAlertAt = sleep.endedAt;
                    }
                    const auto correction = before - prediction.eye();
                    if (ready && walking && glm::length(correction) < 2) visualOffset += correction; else visualOffset = {};
                }
                if (!found) throw std::runtime_error("Server omitted local player state");
            } else if (const auto* dogs = std::get_if<multiplayer::DogFrame>(&message)) {
                snapshots.pushDogs(*dogs,now);
            } else if (const auto* setups = std::get_if<multiplayer::ZombieSetups>(&message)) {
                // Bulk setup delivery can precede Welcome on the other lane.
                // The loaded scene is sufficient to validate and retain it.
                snapshots.pushSetups(scene, *setups);
                multiplayer::ZombieSetupAck ack;
                for (const auto& setup : setups->zombies) ack.zombies.push_back({setup.id, setup.version});
                if (!network.send(connection, ack, true)) throw std::runtime_error("Cannot acknowledge zombie setup");
            } else if (const auto* pose = std::get_if<multiplayer::CorpsePose>(&message)) {
                snapshots.pushPose(scene, *pose, now, network.estimatedOneWaySeconds(connection));
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
        if (self) appearanceUpload.pump([&](const auto& chunk) { return network.send(connection, chunk, true); });
        if (now - lastSnapshot > (prediction.ready() ? 5 : 15)) throw std::runtime_error(prediction.ready() ? "Connection lost: server stopped responding (snapshot timeout)" :
                                     "Could not join: server did not provide player state within 15 seconds");
        if (prediction.ready()) {
            accumulator += dt;
#ifdef MP_GRAPHICS
            std::optional<multiplayer::FirearmShot> frameFirearmShot;
            std::vector<multiplayer::FirearmImpact> frameFirearmImpacts;
#endif
            while (accumulator >= 1.0 / 60) {
                auto nextCommand = command(in, graphics.window, options.bot, now, swayScale());
#ifdef MP_GRAPHICS
                const auto& equipped = weaponDefinitions.equipped();
                if (graphics.renderer && nextCommand.left && !nextCommand.objectHandling && nextCommand.slot != viewer::handlingSlot && !nextCommand.reload &&
                    !in.driver.occupied() && in.equippedSlot < viewer::weaponIds.size() && !equipped.config.grenade) {
                    const auto aim = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, now, true, swayScale());
                    // The renderer retains its last displayed transforms/frames,
                    // even when newly received state has already moved replicas.
                    if (!frameFirearmShot)
                        frameFirearmShot = reportFirearmHits(static_cast<std::uint8_t>(in.equippedSlot),
                            equipped.config, aim.position(), aim.direction(), [&](glm::vec3 origin, glm::vec3 direction) {
                                return graphics.renderer->traceZombieShot(origin, direction, *scene.collision);
                            }, &frameFirearmImpacts, [&](glm::vec3 origin, glm::vec3 direction) {
                                return graphics.renderer->traceDogShot(origin,direction,*scene.collision);
                            }, in.shotInaccuracy.sample(hipFireSpread()));
                    nextCommand.firearmShot = frameFirearmShot;
                }
                // Predict presentation before enqueueing so its mist marker is
                // retained in the same numbered command and all retransmits.
                nextCommand.sequence = prediction.nextSequence();
                multiplayer::WeaponFeedback feedback;
                if (inventory) {
                    feedback = graphics.weaponFeedback.advance(nextCommand, now, weaponDefinitions, *inventory);
                    if (feedback.firearmSlot) {
                        const auto slot = *feedback.firearmSlot;
                        // The hit trace above used the displayed pre-shot aim.
                        // Recoil now changes presentation and the actual muzzle
                        // sent to observers, without changing that shot's ray.
                        in.weaponMotion.fire(slot, now);
                        graphics.muzzleSlot = slot;
                        graphics.muzzleModelPosition = weaponDefinitions.state(slot).config.muzzlePosition;
                        graphics.muzzleFlash.fire(now, {});
                    }
                    if (nextCommand.firearmShot && graphics.weapon) {
                        const auto slot = nextCommand.firearmShot->slot;
                        const auto eye = in.weaponMotion.aimCamera(in.camera, slot, now, true, swayScale());
                        nextCommand.firearmShot->muzzlePosition = glm::vec3(glm::inverse(eye.view()) *
                            glm::vec4(graphics.weapon->muzzlePosition(slot, weaponDefinitions.state(slot).config.muzzlePosition,
                                in.weaponMotion.pose(slot, now)), 1));
                    }
                    if (feedback.firearmSlot && nextCommand.firearmShot && !frameFirearmImpacts.empty()) {
                        for (const auto& impact : frameFirearmImpacts)
                            graphics.localBloodMist.emit(impact.position, impact.size, now);
                        nextCommand.firearmShot->predictedMist = true;
                    }
                }
                const auto& controller = prediction.controller();
                const viewer::UndergrowthMotion shrubBefore{controller.feet(), controller.bodyHeight(),
                    controller.config().radius, controller.grounded(), false};
#endif
                scene.floorplans->update({prediction.eye()});
                const auto previousFeet = prediction.controller().feet();
                const auto predicted = prediction.advance(nextCommand, *scene.collision);
                const auto movement = prediction.controller().feet() - previousFeet;
                playerSpeed = glm::length(glm::vec2(movement.x, movement.z)) / multiplayer::tickSeconds;
                if (!predicted.inventoryActions.empty()) lastInventoryCommand = predicted.sequence;
#ifdef MP_GRAPHICS
                graphics.speedMetersPerSecond = playerSpeed;
                // Fresh prediction only: reconciliation replays must stay silent.
                if (graphics.audio) {
                    const bool running = nextCommand.movement.run && controller.stance() == viewer::PlayerStance::Standing;
                    graphics.audio->playWeapon(feedback.sounds);
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
                        !in.driver.occupied() && !in.handling(), swayScale());
                    int viewWidth = 0, viewHeight = 0;
                    glfwGetFramebufferSize(graphics.window, &viewWidth, &viewHeight);
                    if (viewWidth > 0 && viewHeight > 0) {
                        const auto renderSize = graphics.settings->renderSize();
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
        scene.floorplans->update({prediction.ready() ? prediction.eye() : scene.spawnEye});
        snapshots.apply(scene, now);
#ifdef MP_GRAPHICS
        if (graphics.window && !snapshots.empty()) {
            // Shots share one event decision for sound, remote recoil and flash.
            // Check every frame so an event held for buffered playback can become
            // due even when no newer network snapshot arrives.
            std::vector<multiplayer::SoundEvent> shots;
            const auto cues = graphics.remoteEvents.receive(snapshots.latest(), self, &shots,
                                                            snapshots.animationSeconds());
            graphics.audio->queueRemote(cues);
            graphics.players->receiveShots(shots, snapshots.players(), snapshots.animationSeconds());
        }
#endif
        visualOffset *= static_cast<float>(std::exp(-15 * dt));
        if (prediction.ready()) {
            if (in.toggleDrivingCamera) {
                in.driver.toggleThirdPerson(*scene.vehicles, in.camera, *scene.collision);
                in.toggleDrivingCamera = false;
            }
            if (in.driver.occupied()) in.driver.updateCamera(*scene.vehicles, in.camera, *scene.collision);
            else in.camera.setPosition(prediction.presentedEye(static_cast<float>(accumulator * 60)) + visualOffset);
        }
        if (options.bot) in.camera.setOrientation(0, 0);
#ifdef MP_GRAPHICS
        if (graphics.window && (!feedback || prediction.ready())) {
            graphics.audio->update(in.camera, scene, snapshots, self, static_cast<float>(dt), now);
            int width = 0, height = 0; glfwGetFramebufferSize(graphics.window, &width, &height);
            if (width > 0 && height > 0) {
                const auto renderSize = graphics.settings->renderSize();
                const int renderWidth = renderSize.x, renderHeight = renderSize.y;
                const float renderAspect = float(renderWidth) / renderHeight;
                intervalSimulationSeconds += std::chrono::duration<double>(Clock::now() - simulationStart).count();
                auto& renderer = *graphics.renderer;
                renderer.setFlashlight(in.flashlight);
                renderer.setRemoteFlashlights(multiplayer::remoteFlashlights(snapshots.players(), self));
                renderer.setLocalBloodMist(graphics.localBloodMist.sample(now));
                renderer.setBloodEffectSeconds(now);
                renderer.setFireEffects(snapshots.fireEffects(),now);
                renderer.updateGrenades(snapshots.grenades());
                renderer.updateVegetationBlasts(snapshots.explosions(now));
                auto pointLights = snapshots.explosionLights(now);
                const auto remoteFlashes = graphics.players->muzzleLights(snapshots.animationSeconds());
                pointLights.insert(pointLights.end(), remoteFlashes.begin(), remoteFlashes.end());
                renderer.setExplosionLights(pointLights);
                const auto viewCamera = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, now, !in.driver.occupied() && !in.handling(), swayScale());
                auto flash = graphics.muzzleFlash.light(now);
                flash.position = graphics.weapon->muzzlePosition(graphics.muzzleSlot, graphics.muzzleModelPosition,
                    in.weaponMotion.pose(graphics.muzzleSlot, now, in.temporaryHandling()));
                renderer.setMuzzleFlash(flash.transformed(glm::inverse(viewCamera.view())));
                renderer.resize(renderWidth, renderHeight); renderer.updateDoors(*scene.doors); renderer.updateBoxes(*scene.boxes);
                renderer.updateVehicles(*scene.vehicles, in.driver.car());
                const auto lighting = viewer::dayNightLighting(scene.dayNight.hourAt(snapshots.animationSeconds()));
                const auto dayNightTint = viewer::dayNightDisplayTint(lighting);
                renderer.setDayNightLighting(lighting);
                if (!in.inSleepSession) renderer.updateZombies(scene.zombies);
                viewer::PlayerBodyPose ownBody;
                if (prediction.ready() && !in.driver.occupied() && !in.handling() && in.slot==viewer::punchSlot && ownState.slot==viewer::punchSlot) {
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
                renderer.updateFloorplans(*scene.floorplans);
                if (in.inSleepSession) renderer.renderBlack();
                else renderer.render(viewCamera, snapshots.animationSeconds(), [&] {
                    graphics.birds->draw(viewCamera,renderAspect,snapshots.animationSeconds(),birds.sample(snapshots.animationSeconds()),
                                         dayNightTint);
                    graphics.players->draw(viewCamera, snapshots.players(), self, renderWidth, renderHeight,
                                           snapshots.animationSeconds(), dayNightTint);
                    graphics.body->drawPose(ownBody,viewCamera.projection(renderAspect)*viewCamera.view(),renderWidth,renderHeight);
                });
                if (!in.inSleepSession && prediction.ready() && !in.driver.occupied() && in.equippedSlot != viewer::handlingSlot &&
                    in.camera.viewMode() == viewer::Camera::ViewMode::original &&
                    !in.weaponSwitching && viewer::weaponViewVisible(weaponDefinitions.equipped().config, ownState.loaded, ownState.weaponCoolingDown))
                    graphics.weapon->draw(ownState.slot, renderWidth, renderHeight, flash,
                                          in.weaponMotion.pose(ownState.slot, now, in.temporaryHandling()));
                const auto overlayViewport = renderer.beginOverlay(width, height);
                if (!in.inSleepSession && !in.driver.occupied()) graphics.reticle->draw(in.camera.viewMode(), overlayViewport.width, overlayViewport.height,
                    viewer::inaccuracyCrosshairRadius(hipFireSpread(), viewCamera.projection(renderAspect)[1][1]));
                viewer::HudState hudState;
                hudState.seconds = now;
                hudState.gameSeconds = snapshots.animationSeconds();
                hudState.dayNight = scene.dayNight;
                hudState.localPlayer = self;
                if (!snapshots.empty()) hudState.sleepPlan = snapshots.latest().sleepPlan;
                hudState.screen = in.hudScreen;
                hudState.feet = prediction.controller().feet();
                hudState.forward = in.camera.direction();
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
                hudState.mode = in.handling() ? "OBJECT HANDLING" : in.equippedSlot >= viewer::punchSlot ? "SKILL" : "ITEM";
                if (const auto car = in.driver.car()) {
                    const auto velocity = scene.vehicles->velocity(*car);
                    hudState.speedMetersPerSecond = glm::length(glm::vec2(velocity.x, velocity.z));
                } else hudState.speedMetersPerSecond = graphics.speedMetersPerSecond;
                hudState.mouseCaptured = in.capture;
                hudState.objectHandling = in.handling() && !in.driver.occupied();
                hudState.handInventoryActive = ownState.handInventoryActive;
                hudState.handAccessRemaining = ownState.handAccessRemaining;
                hudState.handStoreRejections = ownState.handStoreRejections;
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
                    hudState.stamina = prediction.controller().exertion().stamina;
                    hudState.strain = prediction.controller().exertion().strain;
                    hudState.lastDamageSeconds = lastDamageSeconds;
                    hudState.equipment = weaponDefinitions.equipped().config.name;
                    hudState.slot = ownState.slot;
                    if (ownState.slot < viewer::weaponIds.size()) {
                        hudState.loaded = static_cast<int>(ownState.loaded);
                        hudState.reserve = static_cast<int>(ownState.reserve);
                    }
                }
                if (in.driver.occupied()) hudState.prompt = "Enter: exit vehicle";
                else if (in.handling())
                    hudState.prompt = "Hold left: drag; release left: drop; hold right: throw / push";
                else if (birds.ready() && ownState.health>0)
                    for (const auto& flock:birds.simulation().state().flocks)
                        if (flock.control.mode==viewer::boid::Mode::gathering || flock.control.mode==viewer::boid::Mode::swooping) {
                            hudState.prompt="Birds diving! Move aside, duck or reach cover"; break;
                        }
                graphics.hud->draw(hudState, width, height);
                if (const auto requested = graphics.hud->takeScreenRequest()) showHudScreen(graphics.window, *requested);
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
                        (in.driver.occupied() ? (in.driver.driving() ? "Driving | WASD, Space brake, Enter exit, Alt camera" : "Passenger | Mouse look, Enter exit, Alt camera") :
                         in.handling() ? "Object handling" : "Combat") + (ownState.slot==viewer::punchSlot ? " | Fists: left click punch" :
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
        << " zombies=" << snapshots.trackedZombieIds().size()
        << " dogs=" << std::count_if(scene.dogs.instances.begin(),scene.dogs.instances.end(),[](const auto& dog){return !dog.removed;})
        << " moving_dogs=" << std::count_if(scene.dogs.instances.begin(),scene.dogs.instances.end(),[](const auto& dog){return !dog.removed && dog.walking;}) << '\n';
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
