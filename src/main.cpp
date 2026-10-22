#include "renderer.hpp"
#include "door_layer.hpp"
#include "player_interaction.hpp"
#include "physics_world.hpp"
#include "box_layer.hpp"
#include "physics_scene.hpp"
#include "interior_objects.hpp"
#include "reticle_overlay.hpp"
#include "weapons.hpp"
#include "weapon_overlay.hpp"
#include "camera_rig.hpp"
#include "collision_debug.hpp"
#include "gpu_timer.hpp"
#include "tree_layer.hpp"
#include "pole_layer.hpp"
#include "guardrail_layer.hpp"
#include "model_merge.hpp"
#include "forest_layer.hpp"
#include "vegetation_selection.hpp"
#include "zombie_layer.hpp"
#include "zombie_navigation.hpp"
#include "zombie_scenario.hpp"
#include "zombie_door_collision.hpp"
#include "zombie_park.hpp"
#include "simulation_clock.hpp"
#include "scene_audio.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Options {
    std::filesystem::path model = VIEWER_DEFAULT_ASSET;
    std::filesystem::path shaders = VIEWER_SHADER_DIR;
    std::filesystem::path screenshot;
    std::filesystem::path trees;
    std::filesystem::path poles;
    std::filesystem::path guardrails;
    std::filesystem::path zombies;
    std::filesystem::path scenario;
    int frames = 0;
    bool hidden = false, allowSoftware = false, selfTest = false, help = false;
    bool fps = false, collisionDebug = false, selfTestFPS = false;
    bool shadows = true;
    int sunShadowResolution = viewer::SunLighting::shadowResolution;
    bool treeShadows = true;
    bool treeCanopyShading = true;
    bool treeDepthOrdering = true;
    bool treeCardSplitting = true;
    bool treeNearestFiltering = true;
    float treeBillboardDistance = 100;
    float oldForestDistance = viewer::OldForestVisibility::distance;
    bool night = false;
    viewer::DayNightCycle dayNight;
    bool explicitDayNight = false;
    bool rain = false;
    viewer::AudioOptions audio{VIEWER_AUDIO_DIR};
    bool noForests = false;
    viewer::VegetationSelection vegetation;
    bool noFences = false;
    bool noPoles = false;
    bool noGuardrails = false;
    bool roofBatching = true;
    bool wallBatching = true;
    bool haze = true;
    bool groundFog = true;
    bool noTrees = false, treeCulling = true, treeCollisions = true, sceneCulling = true;
    bool noZombies = false, zombieView = false, explicitZombies = false, zombieCulling = true;
    bool zombiePerceptionStaggering = true;
    bool zombieDistanceDetail = true, zombieShadows = true, profile = false;
    bool flashlightShadows = false;
    std::optional<float> zombieDrawDistance;
    int zombieCount = 1000;
    bool explicitZombieCount = false, explicitZombieRadius = false;
    float zombieRadius = 100.0f;
    std::optional<double> animationTime;
    std::optional<glm::vec2> spawn;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    bool hasModel = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value after " + argument);
            return argv[i];
        };
        if (argument == "--help" || argument == "-h") options.help = true;
        else if (argument == "--profile") options.profile = true;
        else if (argument == "--sun-shadow-resolution") {
            const auto text=value();std::size_t used=0;
            try { options.sunShadowResolution=std::stoi(text,&used); }
            catch(const std::exception&) { throw std::runtime_error(argument+" requires integer pixels (256..8192)"); }
            if(used!=text.size() || options.sunShadowResolution<256 || options.sunShadowResolution>8192)
                throw std::runtime_error(argument+" requires integer pixels (256..8192)");
        }
        else if (argument == "--tree-billboard-distance" || argument == "--old-forest-distance") {
            auto& meters = argument == "--tree-billboard-distance" ? options.treeBillboardDistance : options.oldForestDistance;
            const auto text = value(); std::size_t used = 0;
            try { meters = std::stof(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(argument + " requires nonnegative meters (0 disables)"); }
            if (used != text.size() || !std::isfinite(meters) || meters < 0)
                throw std::runtime_error(argument + " requires nonnegative meters (0 disables)");
        }
        else if (argument == "--no-zombie-lod") options.zombieDistanceDetail = false;
        else if (argument == "--flashlight-shadows") options.flashlightShadows = true;
        else if (argument == "--no-flashlight-shadows") options.flashlightShadows = false;
        else if (argument == "--no-zombie-shadows") options.zombieShadows = false;
        else if (argument == "--zombie-draw-distance") {
            const auto text = value();
            std::size_t used = 0;
            float meters;
            try { meters = std::stof(text, &used); }
            catch (const std::exception&) { throw std::runtime_error("--zombie-draw-distance requires a nonnegative distance in meters"); }
            if (used != text.size() || !std::isfinite(meters) || meters < 0)
                throw std::runtime_error("--zombie-draw-distance requires a nonnegative distance in meters");
            options.zombieDrawDistance = meters;
        }
        else if (argument == "--hidden") options.hidden = true;
        else if (argument == "--allow-software") options.allowSoftware = true;
        else if (argument == "--self-test-input") options.selfTest = true;
        else if (argument == "--fps") options.fps = true;
        else if (argument == "--night") options.night = true;
        else if (argument == "--time-of-day" || argument == "--day-length") {
            const auto text = value();
            std::size_t used = 0;
            double number;
            const auto message = argument == "--time-of-day"
                ? "--time-of-day requires an hour in [0, 24)"
                : "--day-length requires zero (frozen) or at least one second";
            try { number = std::stod(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(message); }
            if (used != text.size() || !std::isfinite(number) || number < 0.0 ||
                (argument == "--time-of-day" ? number >= 24.0 : number > 0.0 && number < 1.0))
                throw std::runtime_error(message);
            if (argument == "--time-of-day") options.dayNight.startHour = number;
            else options.dayNight.dayLengthSeconds = number;
            options.explicitDayNight = true;
        }
        else if (argument == "--rain") options.rain = true;
        else if (argument == "--no-audio") options.audio.enabled = false;
        else if (argument == "--no-hrtf") options.audio.hrtf = false;
        else if (argument == "--audio-dir") options.audio.directory = value();
        else if (argument == "--no-shadows") options.shadows = false;
        else if (argument == "--no-tree-shadows") options.treeShadows = false;
        else if (argument == "--no-tree-canopy-shading") options.treeCanopyShading = false;
        else if (argument == "--no-haze") options.haze = false;
        else if (argument == "--no-ground-fog") options.groundFog = false;
        else if (argument == "--collision-debug") options.collisionDebug = true;
        else if (argument == "--self-test-fps") options.selfTestFPS = true;
        else if (argument == "--trees") options.trees = value();
        else if (argument == "--poles") options.poles = value();
        else if (argument == "--guardrails") options.guardrails = value();
        else if (argument == "--no-guardrails") options.noGuardrails = true;
        else if (argument == "--no-poles") options.noPoles = true;
        else if (argument == "--forests" || argument == "--forest")
            throw std::runtime_error("--forests was removed; choose --vegetation osm-tuned2, orthophoto, osm, sentinel-astra or none");
        else if (argument == "--vegetation") options.vegetation.select(value());
        else if (argument == "--osm-vegetation")
            throw std::runtime_error("--osm-vegetation was replaced by --vegetation osm");
        else if (argument == "--no-forests") options.noForests = true;
        else if (argument == "--no-fences") options.noFences = true;
        else if (argument == "--no-roof-batching") options.roofBatching = false;
        else if (argument == "--no-wall-batching") options.wallBatching = false;
        else if (argument == "--no-trees") options.noTrees = true;
        else if (argument == "--no-tree-culling") options.treeCulling = false;
        else if (argument == "--no-tree-depth-ordering") options.treeDepthOrdering = false;
        else if (argument == "--no-tree-card-splitting") options.treeCardSplitting = false;
        else if (argument == "--no-tree-nearest-filtering") options.treeNearestFiltering = false;
        else if (argument == "--no-zombie-culling") options.zombieCulling = false;
        else if (argument == "--no-zombie-staggering") options.zombiePerceptionStaggering = false;
        else if (argument == "--no-town-culling") options.sceneCulling = false;
        else if (argument == "--no-tree-collisions") options.treeCollisions = false;
        else if (argument == "--zombies") { options.zombies = value(); options.explicitZombies = true; }
        else if (argument == "--scenario") {
            if (!options.scenario.empty()) throw std::runtime_error("Repeated --scenario option");
            options.scenario = value();
            if (options.scenario.empty() || options.scenario.string().rfind("--", 0) == 0)
                throw std::runtime_error("--scenario requires a scenario filename");
        }
        else if (argument == "--no-zombies") options.noZombies = true;
        else if (argument == "--zombie-view") { options.zombieView = true; options.explicitZombies = true; }
        else if (argument == "--zombie-count") {
            const auto text = value();
            std::size_t used = 0;
            try { options.zombieCount = std::stoi(text, &used); }
            catch (const std::exception&) { throw std::runtime_error("--zombie-count requires an integer from 1 to 100000"); }
            if (used != text.size() || options.zombieCount < 1 || options.zombieCount > 100000)
                throw std::runtime_error("--zombie-count requires an integer from 1 to 100000");
            options.explicitZombies = true;
            options.explicitZombieCount = true;
        }
        else if (argument == "--zombie-radius" || argument == "--animation-time") {
            const auto text = value();
            std::size_t used = 0;
            double number;
            try { number = std::stod(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(argument + " requires a finite number"); }
            if (used != text.size() || !std::isfinite(number) || number < 0 ||
                (argument == "--zombie-radius" && (number <= 0 || number > 1000)))
                throw std::runtime_error(argument + " requires nonnegative seconds or a radius in (0, 1000] meters");
            if (argument == "--animation-time") options.animationTime = number;
            else {
                options.zombieRadius = static_cast<float>(number);
                options.explicitZombies = true;
                options.explicitZombieRadius = true;
            }
        }
        else if (argument == "--spawn") {
            auto coordinate = [&]() {
                const auto text = value();
                std::size_t used = 0;
                float number;
                try { number = std::stof(text, &used); }
                catch (const std::exception&) { throw std::runtime_error("--spawn requires finite X and Z coordinates in meters"); }
                if (used != text.size() || !std::isfinite(number))
                    throw std::runtime_error("--spawn requires finite X and Z coordinates in meters");
                return number;
            };
            const float x = coordinate(), z = coordinate();
            options.spawn = glm::vec2(x, z);
            options.fps = true;
        }
        else if (argument == "--screenshot") options.screenshot = value();
        else if (argument == "--shader-dir") options.shaders = value();
        else if (argument == "--frames") {
            const auto text = value();
            std::size_t used = 0;
            try { options.frames = std::stoi(text, &used); }
            catch (const std::exception&) { throw std::runtime_error("--frames requires a positive integer"); }
            if (used != text.size() || options.frames < 1)
                throw std::runtime_error("--frames requires a positive integer");
        } else if (!argument.empty() && argument[0] == '-') throw std::runtime_error("Unknown option: " + argument);
        else if (hasModel) throw std::runtime_error("Only one model filepath may be supplied");
        else { options.model = argument; hasModel = true; }
    }
    if ((options.hidden || options.selfTest || options.selfTestFPS || !options.screenshot.empty()) && options.frames == 0) options.frames = 3;
    if (options.noTrees && !options.trees.empty())
        throw std::runtime_error("Use either --trees FILE or --no-trees");
    (void)viewer::vegetationPaths(options.vegetation, options.model, VIEWER_FOREST_CONFIG,
                                  options.trees);
    if (options.noPoles && !options.poles.empty())
        throw std::runtime_error("Use either --poles FILE or --no-poles");
    if (options.noGuardrails && !options.guardrails.empty())
        throw std::runtime_error("Use either --guardrails FILE or --no-guardrails");
    if (options.noZombies && options.explicitZombies)
        throw std::runtime_error("Use zombie options or --no-zombies, not both");
    if (options.noZombies && !options.scenario.empty())
        throw std::runtime_error("--scenario requires zombies; omit --no-zombies");
    if (options.zombieView && options.fps)
        throw std::runtime_error("--zombie-view uses a free-fly overview; omit --fps/--spawn");
    if (options.night && options.explicitDayNight)
        throw std::runtime_error("Use --night for fixed night or --time-of-day/--day-length for the cycle");
    return options;
}

void printHelp() {
    std::cout << "Usage: godollo_viewer [model.glb|model.gltf] [options]\n"
              << "Default model: " << VIEWER_DEFAULT_ASSET << "\n\n"
              << "W/S forward/back, A/D strafe, Q/E down/up, mouse look, Shift faster\n"
              << "Tab switch Combat/Interaction; left mouse shoot in Combat, grab a door (1.5 m) or box (2 m) in Interaction\n"
              << "Interaction: left/right mouse hold boxes with separate hands; mouse sideways/up/down moves boxes; WASD moves grips\n"
              << "Release to drop; swing and release to throw. Heavy boxes drag; light boxes lift. Door mouse up/down still pushes/pulls\n"
              << "Combat: 1 pistol, 2 SMG, 3 shotgun, 4 sniper, 5 HE grenade; R reload; hold left mouse for automatic fire\n"
              << "Combat: scoped weapons use right mouse to cycle normal, 10-degree, and 1-degree views\n"
              << "Left Alt release/capture mouse, wheel change speed, F toggle flashlight, Home frame model, Escape quit\n\n"
              << "F1 free-fly, F2 FPS walking, F3 collision overlay; Space jump in FPS\n\n"
              << "  --fps               Start in walking mode\n"
              << "  --time-of-day H     Starting hour in [0, 24), default 12 (noon)\n"
              << "  --day-length S      Seconds per full day, default 1200; 0 freezes the hour\n"
              << "  --night             Fixed night with a camera flashlight\n"
              << "  --rain              Enable nearby animated rain streaks\n"
              << "  --no-audio          Disable playback; AI hearing stays active\n"
              << "  --no-hrtf           Use ordinary positional audio\n"
              << "  --audio-dir DIR     Load PCM16 WAV effects and ambience from DIR\n"
              << "  --no-shadows        Disable sun and flashlight shadows for comparison\n"
              << "  --sun-shadow-resolution N Sunlight shadow-map width/height (1024; 256..8192)\n"
              << "  --no-tree-shadows   Disable tree casting for both lights\n"
              << "  --no-tree-canopy-shading Disable approximate daytime canopy shade\n"
              << "  --no-tree-depth-ordering Keep tree visibility-query order for comparison\n"
              << "  --tree-billboard-distance M Single-card tree LoD beyond M meters (100; 0 disables)\n"
              << "  --old-forest-distance M Visibility radius inside old forests (100; 0 disables)\n"
              << "  --no-haze           Disable atmospheric distance haze for comparison\n"
              << "  --no-ground-fog     Disable the analytical ground mist layer\n"
              << "  --spawn X Z         Start walking near world X/Z in meters\n"
              << "  --collision-debug   Show colliders and ground normal (F3)\n"
              << "  --trees FILE        Load a tree instance JSON explicitly\n"
              << "  --no-trees          Disable individual trees and runtime forests\n"
              << "  --vegetation MODE  Select osm-tuned2 (default), orthophoto, osm, sentinel-astra or none\n"
              << "  --no-forests        Disable runtime forests, keep individual trees\n"
              << "  --no-fences         Disable fence panels, shadows and collisions\n"
              << "  --poles FILE        Load a mapped pole column JSON explicitly\n"
              << "  --no-poles          Disable pole columns, shadows and collisions\n"
              << "  --guardrails FILE   Load a mapped guardrail GLB layer explicitly\n"
              << "  --no-guardrails     Disable guardrail strips, shadows and collisions\n"
              << "  --no-roof-batching  Draw static roofs individually for comparison\n"
              << "  --no-wall-batching  Draw simple building walls individually for comparison\n"
              << "  --no-tree-culling   Render all tree instances for comparison\n"
              << "  --no-tree-card-splitting Keep full-width healthy tree cards for comparison\n"
              << "  --no-tree-nearest-filtering Restore authored vegetation filtering (usually trilinear)\n"
              << "  --no-zombie-lod     Disable zombie mesh LOD, distance cutoff and stepped animation\n"
              << "  --zombie-draw-distance M Camera cutoff at normal FOV (180 m; overview unlimited; 0 disables)\n"
              << "  --flashlight-shadows Enable nearby flashlight shadows (default off)\n"
              << "  --no-flashlight-shadows Disable nearby flashlight shadows\n"
              << "  --no-zombie-shadows Disable zombie casting for GPU cost comparisons\n"
              << "  --profile           Print frame/GPU/CPU simulation and render timings\n"
              << "  --no-zombie-culling Disable zombie camera-frustum culling for comparison\n"
              << "  --no-zombie-staggering Check zombie sight every simulation step\n"
              << "  --no-town-culling   Render all town draw instances for comparison\n"
              << "  --no-tree-collisions Disable the estimated trunk colliders\n"
              << "  --zombies PATH      Load individual animated GLB/glTF files from PATH\n"
              << "  --scenario FILE     Load zombie spawns and preplanned destinations from TOML\n"
              << "  --zombie-count N    Number of zombies (default 300 in palace gardens, otherwise 1000)\n"
              << "  --zombie-radius M   Use radial placement near spawn/map center (default 100 m)\n"
              << "  --zombie-view       Start with the camera framing the crowd\n"
              << "  --no-zombies        Disable automatic zombies on town-sized terrain\n"
              << "  --animation-time S  Freeze animation at seconds S for repeatable captures\n"
              << "  --self-test-fps     Exercise mode-switch and jump callbacks\n"
              << "  --frames N          Render N frames, then exit\n"
              << "  --screenshot FILE   Save a PPM image; defaults to 3 frames\n"
              << "  --hidden            Hide the window for capture (display still required)\n"
              << "  --allow-software    Permit a CPU OpenGL driver for diagnostics\n"
              << "  --self-test-input   Exercise registered input/resize callbacks\n"
              << "  --shader-dir DIR    Load the viewer shaders from this directory\n";
}

void glfwError(int code, const char* description) {
    std::cerr << "GLFW error " << code << ": " << (description ? description : "unknown") << '\n';
}

struct GLFWLifetime {
    GLFWLifetime() {
        glfwSetErrorCallback(glfwError);
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW. Run from a desktop session with an accessible DISPLAY/XWayland (or configure the native Wayland backend).");
    }
    ~GLFWLifetime() { glfwTerminate(); }
};

struct AppState {
    AppState(const viewer::CollisionWorld& collision, const viewer::Model& model)
        : rig(collision, model.boundsMin, model.boundsMax, 1280.0f / 720.0f), world(collision) {}
    viewer::CameraRig rig;
    viewer::PlayerInteraction interaction;
    viewer::Weapons weapons{VIEWER_ITEMS, VIEWER_INVENTORY};
    viewer::WeaponMotion weaponMotion;
    viewer::DoorLayer* doors = nullptr;
    viewer::BoxLayer* boxes = nullptr;
    viewer::PhysicsScene* physicsScene = nullptr;
    viewer::InteriorObjects* interiors = nullptr;
    const viewer::CollisionWorld& world;
    viewer::ZombieLayer* zombies = nullptr;
    viewer::SceneAudio* audio = nullptr;
    viewer::Grenades* grenades = nullptr;
    double displayedAnimationSeconds = 0.0;
    glm::vec3 boundsMin, boundsMax;
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    bool proneToggleQueued = false;
    int width = 1280, height = 720;
    bool capture = true, firstMouse = true, hidden = false;
    bool collisionDebug = false;
    bool flashlight = true;
    glm::vec3 playerVelocity{0};
    double lastX = 0, lastY = 0;
};

AppState& state(GLFWwindow* window) { return *static_cast<AppState*>(glfwGetWindowUserPointer(window)); }

void captureMouse(GLFWwindow* window, bool captured) {
    auto& app = state(window);
    app.capture = captured;
    if (!captured) { app.interaction.cancel(); app.weapons.cancelTrigger(); }
    app.firstMouse = true;
    if (!app.hidden) glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void framebufferCallback(GLFWwindow* window, int width, int height) {
    auto& app = state(window);
    app.width = width;
    app.height = height;
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto& app = state(window);
    if (key >= 0 && key <= GLFW_KEY_LAST) app.keys[static_cast<std::size_t>(key)] = action != GLFW_RELEASE;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    else if (key == GLFW_KEY_TAB) {
        app.weapons.cancelTrigger();
        app.interaction.toggleMode();
        std::cout << "Control mode: " << app.interaction.modeName() << '\n';
    } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) {
        if (app.weapons.equip(static_cast<std::size_t>(key - GLFW_KEY_1))) {
            while (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original)
                app.rig.camera().cycleViewMode();
            std::cout << app.weapons.status() << '\n';
        }
    } else if (key == GLFW_KEY_R) {
        if (app.weapons.reload() > 0 && app.audio) app.audio->reload(app.weapons.slot());
        std::cout << app.weapons.status() << '\n';
    } else if (key == GLFW_KEY_LEFT_ALT) captureMouse(window, !app.capture);
    else if (key == GLFW_KEY_F) {
        app.flashlight = !app.flashlight;
        std::cout << "Flashlight: " << (app.flashlight ? "on" : "off") << '\n';
    } else if (key == GLFW_KEY_HOME) {
        app.weapons.cancelTrigger();
        app.interaction.cancel();
        app.rig.frame(static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
    } else if (key == GLFW_KEY_F1) {
        app.proneToggleQueued = false;
        app.weapons.cancelTrigger();
        app.interaction.cancel();
        app.rig.enterFreeFly();
    } else if (key == GLFW_KEY_F2) {
        app.weapons.cancelTrigger();
        app.interaction.cancel();
        try { app.rig.enterWalking(); }
        catch (const std::exception& error) { std::cerr << "FPS placement failed: " << error.what() << '\n'; }
    } else if (key == GLFW_KEY_F3) {
        app.collisionDebug = !app.collisionDebug;
        std::cout << "Collision overlay: " << (app.collisionDebug ? "on" : "off") << '\n';
    } else if (key == GLFW_KEY_Z && app.rig.walking()) {
        // GLFW key tokens identify US-layout physical positions. This is the
        // bottom-row key left of X, labelled Y on a QWERTZ keyboard.
        app.proneToggleQueued = !app.proneToggleQueued;
    }
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto& app = state(window);
    if (!app.capture) return;
    if (!app.firstMouse && std::isfinite(x) && std::isfinite(y))
        app.interaction.look(app.rig.camera(), static_cast<float>(x - app.lastX), static_cast<float>(app.lastY - y), glfwGetTime());
    app.lastX = x;
    app.lastY = y;
    app.firstMouse = false;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    auto& app = state(window);
    if ((button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) && action == GLFW_RELEASE) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) app.weapons.trigger(false);
        app.interaction.release(button == GLFW_MOUSE_BUTTON_LEFT ? viewer::Hand::Left : viewer::Hand::Right, glfwGetTime());
        return;
    }
    if (action != GLFW_PRESS || !app.capture) return;
    if (button == GLFW_MOUSE_BUTTON_RIGHT && app.interaction.mode() == viewer::ControlMode::Combat) {
        if (app.weapons.equipped().config.hasScope) app.rig.camera().cycleViewMode();
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    if (app.interaction.mode() == viewer::ControlMode::Interaction) {
        const auto hand = button == GLFW_MOUSE_BUTTON_LEFT ? viewer::Hand::Left : viewer::Hand::Right;
        if (app.doors && app.boxes && app.interaction.press(hand, app.rig.camera(), *app.doors, *app.boxes, app.world, glfwGetTime()) &&
            app.interaction.grabbedDoor() && hand == viewer::Hand::Left) {
            const auto id = *app.interaction.grabbedDoor();
            if (app.audio) app.audio->grabDoor(*app.doors, id);
            if (app.interiors) {
                const auto added = app.interiors->populate(id, app.world, *app.physicsScene, *app.boxes);
                if (added) std::cout << "Interior: spawned " << added << " physical boxes\n";
            }
        }
        return;
    }
    app.weapons.trigger(true);
}

void scrollCallback(GLFWwindow* window, double, double y) {
    auto& rig = state(window).rig;
    if (!rig.walking()) rig.camera().adjustSpeed(std::pow(1.2f, static_cast<float>(std::clamp(y, -20.0, 20.0))));
}

void focusCallback(GLFWwindow* window, int focused) {
    if (!focused) {
        state(window).keys.fill(false);
        state(window).proneToggleQueued = false;
        state(window).firstMouse = true;
        state(window).interaction.cancel();
        state(window).weapons.cancelTrigger();
    }
}

// True only for a firearm shot that should pulse the held muzzle light.
bool updateWeapons(AppState& app, double seconds) {
    if (!app.capture || app.interaction.mode() != viewer::ControlMode::Combat) {
        app.weapons.cancelTrigger();
        return false;
    }
    const auto& config = app.weapons.equipped().config;
    if (config.grenade && (!app.grenades || !app.grenades->canThrow(app.rig.camera().position(), app.world))) {
        app.weapons.cancelTrigger();
        return false;
    }
    const auto result = app.weapons.fireAttempt(seconds);
    if (result == viewer::WeaponFireResult::empty && app.audio) app.audio->emptyClick();
    if (result != viewer::WeaponFireResult::fired) return false;
    const auto aim = app.weaponMotion.aimCamera(app.rig.camera(), app.weapons.slot(), seconds, true);
    if (config.grenade) {
        if (!app.grenades->throwFrom(app.rig.camera().position(), app.rig.camera().direction(),
                app.playerVelocity, seconds, *config.grenade, app.world))
            throw std::logic_error("Accepted grenade throw failed");
    } else if (app.audio) app.audio->shot(app.weapons.slot());
    if (app.zombies && !config.grenade) {
        for (const auto direction : viewer::weaponShotDirections(app.weapons.equipped().config, aim.direction()))
            app.zombies->shootWithOcclusion(app.rig.camera().position(), direction,
                                            app.displayedAnimationSeconds, app.world);
    }
    if (!config.grenade) app.weaponMotion.fire(app.weapons.slot(), seconds);
    std::cout << app.weapons.status() << '\n';
    return !config.grenade;
}

void moveCamera(AppState& app, float dt) {
    const auto previousFeet = app.rig.player().feet();
    const auto held = [&](int key) { return app.keys[static_cast<std::size_t>(key)] ? 1.0f : 0.0f; };
    app.rig.update(dt, held(GLFW_KEY_W) - held(GLFW_KEY_S), held(GLFW_KEY_D) - held(GLFW_KEY_A),
                   held(GLFW_KEY_E) - held(GLFW_KEY_Q),
                   app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT], app.keys[GLFW_KEY_SPACE],
                   app.keys[GLFW_KEY_LEFT_CONTROL] || app.keys[GLFW_KEY_RIGHT_CONTROL], app.proneToggleQueued);
    app.playerVelocity = app.rig.walking() && dt > 0 ? (app.rig.player().feet() - previousFeet) / dt : glm::vec3(0);
    if (app.rig.walking()) {
        // Fixed player steps can span several short render frames. Bound the
        // measured horizontal speed and use the actual airborne velocity.
        const float horizontal = glm::length(glm::vec2(app.playerVelocity.x, app.playerVelocity.z));
        const float speed = app.rig.player().movementSpeed(app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]);
        if (horizontal > speed) {
            app.playerVelocity.x *= speed / horizontal;
            app.playerVelocity.z *= speed / horizontal;
        }
        app.playerVelocity.y = app.rig.player().verticalVelocity();
    }
    if (dt > 0) app.proneToggleQueued = false;
    app.interaction.update(app.rig.camera(), dt);
}

void inputSelfTest(GLFWwindow* window) {
    auto& app = state(window);
    // Retrieve the installed callbacks, so this tests the same input path as
    // GLFW events, rather than separate synthetic camera movement code.
    const auto key = glfwSetKeyCallback(window, keyCallback);
    const auto cursor = glfwSetCursorPosCallback(window, cursorCallback);
    const auto mouse = glfwSetMouseButtonCallback(window, mouseButtonCallback);
    const auto resize = glfwSetFramebufferSizeCallback(window, framebufferCallback);
    if (!key || !cursor || !mouse || !resize) throw std::runtime_error("Input callbacks were not registered");
    const auto original = app.rig.camera();
    const auto originalWeapons = app.weapons;
    const bool originalFlashlight = app.flashlight;
    const auto viewBeforeFlashlight = app.rig.camera().view();
    key(window, GLFW_KEY_F, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_F, 0, GLFW_REPEAT, 0);
    key(window, GLFW_KEY_F, 0, GLFW_RELEASE, 0);
    if (app.flashlight == originalFlashlight || app.rig.camera().view() != viewBeforeFlashlight)
        throw std::runtime_error("F must toggle the flashlight once without reframing the camera");
    key(window, GLFW_KEY_F, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_F, 0, GLFW_RELEASE, 0);
    if (app.flashlight != originalFlashlight)
        throw std::runtime_error("A second F press must restore the flashlight");
    const bool wasWalking = app.rig.walking();
    app.rig.enterFreeFly();
    for (int code : {GLFW_KEY_W, GLFW_KEY_S, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_Q, GLFW_KEY_E}) {
        const auto before = app.rig.camera().position();
        key(window, code, 0, GLFW_PRESS, 0);
        moveCamera(app, 0.1f);
        key(window, code, 0, GLFW_RELEASE, 0);
        if (glm::length(app.rig.camera().position() - before) < 1e-5f) throw std::runtime_error("Movement input self-test failed");
    }
    const auto before = app.rig.camera().view();
    app.firstMouse = true;
    cursor(window, 100, 100);
    cursor(window, 145, 80);
    float change = 0;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) change += std::abs(app.rig.camera().view()[c][r] - before[c][r]);
    if (change < 1e-5f) throw std::runtime_error("Mouse input self-test failed");
    const auto originalProjection = app.rig.camera().projection(1.0f);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Unscoped pistol changed the view mode");
    key(window, GLFW_KEY_4, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_4, 0, GLFW_RELEASE, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    const auto tenDegreeProjection = app.rig.camera().projection(1.0f);
    const float tenDegrees = glm::degrees(2.0f * std::atan(1.0f / tenDegreeProjection[1][1]));
    if (std::abs(tenDegrees - 10.0f) > 1e-5f || app.rig.camera().viewMode() != viewer::Camera::ViewMode::zoom10)
        throw std::runtime_error("Right-click 10-degree view self-test failed");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != tenDegreeProjection)
        throw std::runtime_error("Mouse release changed the view mode");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    const auto oneDegreeProjection = app.rig.camera().projection(1.0f);
    const float oneDegree = glm::degrees(2.0f * std::atan(1.0f / oneDegreeProjection[1][1]));
    if (std::abs(oneDegree - 1.0f) > 1e-5f || app.rig.camera().viewMode() != viewer::Camera::ViewMode::zoom1)
        throw std::runtime_error("Right-click 1-degree view self-test failed");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Right-click did not restore the original field of view");
    for (int slot = 0; slot < static_cast<int>(viewer::weaponIds.size()); ++slot) {
        key(window, GLFW_KEY_1 + slot, 0, GLFW_PRESS, 0);
        key(window, GLFW_KEY_1 + slot, 0, GLFW_RELEASE, 0);
        if (app.weapons.slot() != static_cast<std::size_t>(slot))
            throw std::runtime_error("Weapon selection callback failed");
    }
    key(window, GLFW_KEY_3, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_3, 0, GLFW_RELEASE, 0);
    const auto loaded = app.weapons.equipped().loaded;
    mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    updateWeapons(app, 0);
    mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    if (app.weapons.equipped().loaded != loaded - app.weapons.equipped().config.ammoPerShot)
        throw std::runtime_error("Shooting callback did not consume ammo");
    key(window, GLFW_KEY_R, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_R, 0, GLFW_RELEASE, 0);
    if (app.weapons.equipped().loaded != loaded)
        throw std::runtime_error("Reload callback did not refill magazine");
    for (const bool blocked : {true, false}) {
        viewer::Model fixture;
        fixture.boundsMin = {-5, 0, 0}; fixture.boundsMax = {5, 3, 12};
        if (blocked) {
            viewer::Primitive wall;
            for (const auto p : {glm::vec3(-5, 0, 5), {5, 0, 5}, {5, 3, 5}, {-5, 3, 5}}) {
                viewer::Vertex vertex; vertex.position = p; wall.vertices.push_back(vertex);
            }
            wall.indices = {0, 1, 2, 0, 2, 3};
            fixture.primitives.push_back(std::move(wall)); fixture.draws.push_back({});
        }
        const viewer::CollisionWorld world(fixture);
        viewer::ZombieLayer zombies;
        zombies.assets.emplace_back(); zombies.instances.emplace_back();
        auto& zombie = zombies.instances.front();
        zombie.transform[3] = {0, 0, 10, 1};
        zombie.restTransform = zombie.restRoot = zombie.transform;
        AppState shot(world, fixture); shot.zombies = &zombies;
        shot.rig.camera().setPosition({0, .9f, 0}); shot.rig.camera().setOrientation(90, 0);
        shot.weapons.equip(0); shot.weapons.trigger(true);
        const auto rounds = shot.weapons.equipped().loaded;
        updateWeapons(shot, 0);
        if (zombie.ragdoll == blocked || shot.weapons.equipped().loaded != rounds - 1)
            throw std::runtime_error("Single-player shooting occlusion/ammo self-test failed");
        auto grenadePhysics = std::make_shared<viewer::PhysicsWorld>();
        viewer::Grenades thrown(grenadePhysics);
        shot.grenades = &thrown;
        shot.weapons.equip(4);
        const auto grenadeCount = shot.weapons.equipped().loaded;
        shot.weapons.trigger(true);
        if (updateWeapons(shot, 0) || thrown.poses().size() != 1 || shot.weapons.equipped().loaded != grenadeCount - 1)
            throw std::runtime_error("Grenade click must spawn a body and consume one grenade without a muzzle flash");
        updateWeapons(shot, 10);
        if (thrown.poses().size() != 1) throw std::runtime_error("Holding left click repeated a grenade throw");
    }
    key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    if (!app.capture || app.interaction.mode() != viewer::ControlMode::Interaction)
        throw std::runtime_error("Tab failed to select interaction mode with mouse captured");
    key(window, GLFW_KEY_TAB, 0, GLFW_REPEAT, 0);
    if (app.interaction.mode() != viewer::ControlMode::Interaction)
        throw std::runtime_error("Tab repeat changed the control mode");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Interaction right hand changed zoom");
    key(window, GLFW_KEY_TAB, 0, GLFW_RELEASE, 0);
    key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_TAB, 0, GLFW_RELEASE, 0);
    if (app.interaction.mode() != viewer::ControlMode::Combat)
        throw std::runtime_error("Tab failed to restore combat mode");
    key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
    if (app.capture) throw std::runtime_error("Mouse release self-test failed");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Released cursor changed zoom");
    key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_RELEASE, 0);
    key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_RELEASE, 0);
    resize(window, 800, 600);
    if (app.width != 800 || app.height != 600) throw std::runtime_error("Resize callback self-test failed");
    glfwSetWindowSize(window, 1280, 720);
    glfwPollEvents();
    glfwGetFramebufferSize(window, &app.width, &app.height);
    key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    if (!glfwWindowShouldClose(window)) throw std::runtime_error("Escape callback self-test failed");
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    app.keys.fill(false);
    app.rig.camera() = original;
    app.weapons = originalWeapons;
    if (wasWalking && !app.rig.enterWalking()) throw std::runtime_error("Failed to restore walking mode after input test");
    app.firstMouse = true;
    std::cout << "Input self-test passed: movement, mouse capture, control modes, flashlight toggle, weapon selection/fire/reload, shot occlusion, scope, resize, Escape\n";
}

void fpsInputSelfTest(GLFWwindow* window) {
    auto& app = state(window);
    const auto key = glfwSetKeyCallback(window, keyCallback);
    key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
    if (app.rig.walking()) throw std::runtime_error("F1 failed to select free-fly mode");
    key(window, GLFW_KEY_F2, 0, GLFW_PRESS, 0);
    if (!app.rig.walking()) throw std::runtime_error("F2 failed to select FPS mode");
    for (int frame = 0; frame < 120; ++frame) moveCamera(app, 1.0f / 120.0f);
    if (!app.rig.player().grounded()) throw std::runtime_error("FPS player did not settle on the ground");
    const float feet = app.rig.player().feet().y;
    const float eyeHeight = app.rig.camera().position().y - feet;
    if (std::abs(eyeHeight - 1.7f) > 0.003f) throw std::runtime_error("FPS camera is not at human eye height");
    key(window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    moveCamera(app, 1.0f / 60.0f);
    key(window, GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    if (app.rig.player().verticalVelocity() <= 0) throw std::runtime_error("Space failed to start a grounded jump");
    for (int frame = 0; frame < 240; ++frame) moveCamera(app, 1.0f / 120.0f);
    if (!app.rig.player().grounded() || std::abs(app.rig.player().feet().y - feet) > 0.03f)
        throw std::runtime_error("FPS jump failed to return to the supporting ground");
    key(window, GLFW_KEY_LEFT_CONTROL, 0, GLFW_PRESS, 0);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().stance() != viewer::PlayerStance::Crouched)
        throw std::runtime_error("Held Ctrl failed to crouch");
    key(window, GLFW_KEY_LEFT_CONTROL, 0, GLFW_RELEASE, 0);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().stance() != viewer::PlayerStance::Standing)
        throw std::runtime_error("Releasing Ctrl failed to stand");
    key(window, GLFW_KEY_Z, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_Z, 0, GLFW_REPEAT, 0);
    key(window, GLFW_KEY_Z, 0, GLFW_RELEASE, 0);
    moveCamera(app, 1.0f / 120.0f);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().stance() != viewer::PlayerStance::Prone)
        throw std::runtime_error("A short prone press must toggle once, ignore repeats and persist after release");
    key(window, GLFW_KEY_Z, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_Z, 0, GLFW_RELEASE, 0);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().stance() != viewer::PlayerStance::Standing)
        throw std::runtime_error("A second prone press failed to stand");
    const bool overlay = app.collisionDebug;
    key(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    if (app.collisionDebug == overlay) throw std::runtime_error("F3 failed to toggle collision debugging");
    key(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    const auto walkingEye = app.rig.camera().position();
    key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
    if (glm::length(app.rig.camera().position() - walkingEye) > 1e-5f)
        throw std::runtime_error("Leaving FPS mode moved the free-fly camera unexpectedly");
    key(window, GLFW_KEY_F2, 0, GLFW_PRESS, 0);
    app.keys.fill(false);
    std::cout << "FPS input self-test passed: F1/F2 modes, 1.70 m eye height, Space jump/landing, Ctrl crouch, prone toggle, F3 overlay\n";
}

std::vector<viewer::DebugVertex> collisionOverlay(const AppState& app) {
    std::vector<viewer::DebugVertex> vertices;
    const auto near = app.rig.hasPlayer() ? app.rig.player().feet() : app.rig.camera().position();
    for (const auto& point : app.world.debugLines(near, 12.0f))
        vertices.push_back({point, {0.95f, 0.65f, 0.10f}});
    if (!app.rig.hasPlayer()) return vertices;
    const auto& player = app.rig.player();
    const auto& config = player.config();
    const auto color = player.grounded() ? glm::vec3(0.15f, 1.0f, 0.3f) : glm::vec3(1.0f, 0.2f, 0.25f);
    const auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec3 tint) {
        vertices.push_back({a, tint}); vertices.push_back({b, tint});
    };
    const auto feet = player.feet();
    constexpr int slices = 24;
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < slices; ++i) {
        const float a = 2.0f * pi * static_cast<float>(i) / slices;
        const float b = 2.0f * pi * static_cast<float>(i + 1) / slices;
        for (float y : {config.radius, player.bodyHeight() - config.radius}) {
            line(feet + glm::vec3(config.radius * std::cos(a), y, config.radius * std::sin(a)),
                 feet + glm::vec3(config.radius * std::cos(b), y, config.radius * std::sin(b)), color);
        }
        // Two perpendicular great circles show the rounded capsule caps.
        for (int axis = 0; axis < 2; ++axis) {
            const auto capPoint = [&](float angle) {
                const float y = std::sin(angle);
                glm::vec3 p(0, y * config.radius + (y > 0 ? player.bodyHeight() - config.radius : config.radius), 0);
                p[axis == 0 ? 0 : 2] = std::cos(angle) * config.radius;
                return feet + p;
            };
            line(capPoint(a), capPoint(b), color);
        }
    }
    line(feet, feet + player.groundNormal(), {0.2f, 0.9f, 1.0f});
    for (glm::vec3 axis : {glm::vec3(0.12f, 0, 0), glm::vec3(0, 0, 0.12f)})
        line(feet - axis, feet + axis, {1.0f, 1.0f, 1.0f});
    return vertices;
}

void APIENTRY debugMessage(GLenum, GLenum type, GLuint id, GLenum severity,
                          GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::cerr << "OpenGL debug [" << id << ", type 0x" << std::hex << type
              << ", severity 0x" << severity << std::dec << "]: " << message << '\n';
}

void initializeGraphics(bool allowSoftware) {
    const auto text = [](GLenum name) {
        const auto* value = glGetString(name);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string("unavailable");
    };
    const auto renderer = text(GL_RENDERER);
    std::cout << "OpenGL: " << text(GL_VERSION) << "\nRenderer: " << renderer
              << "\nVendor: " << text(GL_VENDOR) << "\nGLSL: " << text(GL_SHADING_LANGUAGE_VERSION) << '\n';
    GLint major = 0, minor = 0, profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if ((major < 3 || (major == 3 && minor < 3)) || !(profile & GL_CONTEXT_CORE_PROFILE_BIT))
        throw std::runtime_error("OpenGL 3.3 or newer Core Profile is required");
    std::string lower = renderer;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool software = lower.find("llvmpipe") != std::string::npos || lower.find("softpipe") != std::string::npos ||
                          lower.find("software") != std::string::npos || lower.find("swrast") != std::string::npos;
    if (software && !allowSoftware)
        throw std::runtime_error("A software OpenGL driver was selected. Enable GPU access/install its driver; --allow-software permits diagnostic rendering only.");
    if (software) std::cerr << "Diagnostic software rendering: GPU acceleration is NOT active.\n";
#ifndef NDEBUG
    if (major > 4 || (major == 4 && minor >= 3) || glfwExtensionSupported("GL_KHR_debug")) {
        const auto callback = reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(glfwGetProcAddress("glDebugMessageCallback"));
        if (callback) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            callback(debugMessage, nullptr);
            std::cout << "OpenGL debug callback enabled\n";
        }
    } else std::cout << "KHR_debug unavailable; using glGetError checks\n";
#endif
    viewer::Renderer::checkErrors("context initialization");
}

int run(const Options& options) {
    std::cout << "Loading: " << options.model << '\n';
    auto model = viewer::loadModel(options.model);
    if (!options.noPoles) {
        const bool explicitPoles = !options.poles.empty();
        const auto path = explicitPoles ? options.poles : options.model.parent_path() / "poles.instances.json";
        if (explicitPoles || std::filesystem::exists(path)) {
            try {
                auto poles = viewer::loadPoleLayer(path, options.model);
                std::cout << "Poles: " << poles.columns.size() << " columns in "
                          << poles.batches.draws.size() << " spatial batches\n";
                viewer::appendPoleLayer(model, std::move(poles));
            } catch (const std::exception& error) {
                if (explicitPoles) throw;
                std::cerr << "Adjacent pole layer skipped: " << error.what()
                          << "\nRun tools/prepare_pole_instances.py for this map.\n";
            }
        }
    }
    if (!options.noGuardrails) {
        const bool explicitGuardrails = !options.guardrails.empty();
        const auto path = explicitGuardrails ? options.guardrails : options.model.parent_path() / "guardrails.glb";
        if (explicitGuardrails || std::filesystem::exists(path)) {
            try {
                auto layer = viewer::loadGuardrailLayer(path, options.model);
                std::cout << "Guardrails: " << layer.ways << " mapped ways, " << layer.lengthMeters
                          << " m in " << layer.model.draws.size() << " spatial batches\n";
                viewer::appendStaticModel(model, std::move(layer.model));
            } catch (const std::exception& error) {
                if (explicitGuardrails) throw;
                std::cerr << "Adjacent guardrail layer skipped: " << error.what()
                          << "\nRun tools/prepare_guardrails.py for this map.\n";
            }
        }
    }
    if (options.noFences)
        std::cout << "Fences disabled: " << viewer::removeFenceDraws(model) << " draw instances removed\n";
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::ZombieDoorCollision zombieDoorCollision(physics);
    std::cout << "Doors: " << doors.count() << " hinged panels; nearby physics activation\n";
    std::size_t triangles = 0;
    for (const auto& draw : model.draws) triangles += model.primitives.at(draw.primitive).indices.size() / 3;
    std::cout << "Loaded " << model.primitives.size() << " primitives, " << model.draws.size()
              << " draw instances, " << triangles << " triangles, " << model.images.size() << " images\n";
    for (const auto& warning : model.warnings) std::cerr << "Model warning: " << warning << '\n';
    std::optional<viewer::TreeLayer> trees;
    const auto vegetation = viewer::vegetationPaths(options.vegetation, options.model,
        VIEWER_FOREST_CONFIG, options.trees);
    if (!options.noTrees && !vegetation.trees.empty()) {
        const bool explicitTrees = vegetation.requireTrees;
        const auto& path = vegetation.trees;
        if (explicitTrees || std::filesystem::exists(path)) {
            try {
                std::cout << "Loading trees: " << path << '\n';
                trees = viewer::loadTreeLayer(path, options.model);
                std::cout << "Trees: " << trees->instances.size() << " instances, " << trees->assets.size()
                          << " shared assets; frustum culling " << (options.treeCulling ? "on" : "off") << '\n';
                for (const auto& asset : trees->assets)
                    for (const auto& warning : asset.model.warnings)
                        std::cerr << "Tree asset " << asset.id << ": " << warning << '\n';
            } catch (const std::exception& error) {
                if (explicitTrees) throw;
                std::cerr << "Adjacent tree layer skipped: " << error.what()
                          << "\nRegenerate placements for this map, or use --trees FILE to select a layer.\n";
            }
        }
    }
    std::optional<viewer::ForestLayer> forests;
    if (!options.noTrees && !options.noForests && !vegetation.forests.empty()) {
        const bool explicitForests = vegetation.requireForests;
        const auto& path = vegetation.forests;
        if (explicitForests || (model.geographicFrame && std::filesystem::exists(path))) {
            try {
                if (!trees) trees.emplace();
                const auto before = trees->instances.size();
                const viewer::CollisionWorld placementWorld(model, {}, &doors);
                std::cout << "Loading runtime forests: " << path << '\n';
                forests = viewer::loadForestLayer(path, options.model, model, placementWorld, *trees);
                std::cout << "Forests: " << forests->patches.size() << " patches, "
                          << trees->instances.size()-before << " deterministic trees/shrubs; individual vegetation within "
                          << forests->detailDistance << " m or through either scope\n";
            } catch (const std::exception& error) {
                if (explicitForests) throw;
                std::cerr << "Runtime forest layer skipped: " << error.what() << '\n';
            }
        }
    }
    std::vector<viewer::TrunkCollider> trunks;
    if (trees && options.treeCollisions) {
        trunks.reserve(trees->instances.size());
        for (const auto& tree : trees->instances) {
            if (!tree.collidable) continue;
            const auto height = tree.dimensions.y;
            const auto radius = std::min(height * .5f,
                glm::clamp(.035f * std::min(tree.dimensions.x, tree.dimensions.z), .10f, .50f));
            trunks.push_back({glm::vec3(tree.transform[3]), radius, height});
        }
    }
    // Town geometry, trunks and live props participate in walking collision.
    // Shared impostor cards stay exclusively in the rendering layer.
    viewer::BoxLayer boxes(physics);
    viewer::Grenades grenades(physics);
    viewer::CollisionWorld collision(model, trunks, &doors);
    const auto undergrowth = trees ? viewer::UndergrowthIndex(*trees) : viewer::UndergrowthIndex{};
    viewer::UndergrowthEmitter undergrowthEmitter;
    collision.setBoxes(&boxes);
    viewer::PhysicsScene physicsScene(physics, collision, model.boundsMin, model.boundsMax);
    viewer::InteriorObjects interiors(model, doors, std::random_device{}());
    const auto& collisionStats = collision.stats();
    std::cout << "Collision world: " << collisionStats.triangles << " triangles, "
              << collisionStats.buildings << " buildings, " << collisionStats.bvhNodes << " BVH nodes, "
              << collisionStats.trunks << " tree trunks\n";
    std::optional<viewer::ZombieLayer> zombies;
    auto navmesh = options.model;
    navmesh.replace_extension(".navmesh");
    const auto extent = model.boundsMax - model.boundsMin;
    const bool townSizedTerrain = collisionStats.terrainTriangles != 0 && extent.x >= 100 && extent.z >= 100;
    if (!options.scenario.empty()) {
        const auto scenario = viewer::loadZombieScenarioConfig(options.scenario);
        const auto path = options.zombies.empty() ? std::filesystem::path(VIEWER_ZOMBIE_ASSETS) : options.zombies;
        zombies = viewer::loadZombieScenario(path, collision, model, navmesh, scenario);
        std::cout << "Scenario: " << options.scenario << "; " << zombies->instances.size()
                  << " zombies with preplanned random destinations\n";
    } else if (!options.noZombies && (options.explicitZombies || townSizedTerrain)) {
        const auto path = options.zombies.empty() ? std::filesystem::path(VIEWER_ZOMBIE_ASSETS) : options.zombies;
        if (options.explicitZombies || viewer::hasZombieModels(path)) {
            const auto middle = (model.boundsMin + model.boundsMax) * .5f;
            const auto center = options.spawn.value_or(glm::vec2(middle.x, middle.z));
            const auto boundary = options.explicitZombieRadius ? std::vector<glm::vec2>{} :
                viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK);
            const auto count = !boundary.empty() && !options.explicitZombieCount ? 300 : options.zombieCount;
            zombies = viewer::loadZombieLayer(path, collision, model, center,
                                              static_cast<std::size_t>(count), options.zombieRadius, boundary);
            if (!boundary.empty()) std::cout << "Zombie placement: evenly spread across Grassalkovich Royal Palace gardens\n";
            std::cout << "Zombies: " << zombies->instances.size() << " instances, " << zombies->assets.size()
                      << " shared animated assets; looping idle with individual phase offsets\n";
        } else std::cerr << "Zombie assets are missing at " << path << "; see assets/zombies/README.md for download instructions.\n";
    }
    if (zombies) {
        zombies->attachPhysics(physics);
        zombies->setPerceptionStaggering(options.zombiePerceptionStaggering);
        if (!zombies->navigation()) zombies->enableNavigation(navmesh);
        const auto& navigation = *zombies->navigation();
        std::cout << "Zombie navigation: " << navmesh
                  << "; g+8h, 50000 nodes, " << navigation.pathArenaBytes() << " path bytes"
                  << "; cache SHA256 " << navigation.meshSha256()
                  << "; embedded replay SHA256 " << navigation.sourceSha256() << '\n';
    }
    GLFWLifetime lifetime;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    if (options.hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const auto title = "Gödöllő Viewer — " + options.model.filename().string();
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(1280, 720, title.c_str(), nullptr, nullptr), glfwDestroyWindow);
    if (!window) throw std::runtime_error("Cannot create an OpenGL 3.3 Core window. Check display access and the graphics driver.");
    glfwMakeContextCurrent(window.get());
    viewer::initializeOpenGL();
    glfwSwapInterval(options.frames > 0 ? 0 : 1);
    initializeGraphics(options.allowSoftware);
    AppState app(collision, model);
    app.boundsMin = model.boundsMin;
    app.boundsMax = model.boundsMax;
    app.hidden = options.hidden;
    app.collisionDebug = options.collisionDebug;
    glfwGetFramebufferSize(window.get(), &app.width, &app.height);
    app.rig.frame(static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
    if (options.fps) {
        auto preferred = (model.boundsMin + model.boundsMax) * 0.5f;
        preferred.y = model.boundsMax.y + 5.0f;
        if (options.spawn) { preferred.x = options.spawn->x; preferred.z = options.spawn->y; }
        if (!app.rig.enterWalking(preferred)) throw std::runtime_error("Unable to find a valid FPS starting position");
    }
    if (options.zombieView && zombies)
        app.rig.camera().frame(zombies->boundsMin, zombies->boundsMax,
            static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
    app.zombies = zombies ? &*zombies : nullptr;
    app.doors = &doors;
    app.boxes = &boxes;
    app.physicsScene = &physicsScene;
    app.interiors = &interiors;
    auto audioOptions = options.audio;
    audioOptions.rain = options.rain;
    viewer::SceneAudio audio(audioOptions);
    app.audio = &audio;
    app.grenades = &grenades;
    glfwSetWindowUserPointer(window.get(), &app);
    glfwSetFramebufferSizeCallback(window.get(), framebufferCallback);
    glfwSetKeyCallback(window.get(), keyCallback);
    glfwSetCursorPosCallback(window.get(), cursorCallback);
    glfwSetMouseButtonCallback(window.get(), mouseButtonCallback);
    glfwSetScrollCallback(window.get(), scrollCallback);
    glfwSetWindowFocusCallback(window.get(), focusCallback);
    captureMouse(window.get(), true);
    if (!options.hidden && glfwRawMouseMotionSupported()) glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    const char* proneKeyName = glfwGetKeyName(GLFW_KEY_Z, 0);
    std::cout << "Camera mode: " << app.rig.modeName() << "; speed: " << app.rig.speed()
              << " m/s; F1 free-fly, F2 walk, F3 collisions, WASD move, Space jump, Ctrl hold crouch/airborne tuck, "
              << (proneKeyName ? proneKeyName : "Z")
              << " toggle prone, mouse look, Tab Combat/Interaction, F flashlight, Home frame scene, Left Alt cursor, Escape quit\n";
    viewer::Renderer renderer(model, options.shaders, trees ? &*trees : nullptr,
                              zombies ? &*zombies : nullptr, forests ? &*forests : nullptr);
    viewer::ReticleOverlay reticle(options.shaders);
    const auto& playerBody = app.rig.player().config();
    viewer::WeaponOverlay weaponOverlay(options.shaders, VIEWER_WEAPON_ASSETS,
                                       {playerBody.height, playerBody.eyeHeight});
    viewer::MuzzleFlash muzzleFlash;
    std::size_t muzzleSlot = 0;
    glm::vec3 muzzleModelPosition{0};
    renderer.loadGrenadeModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) / "he_grenade.glb");
    std::uint32_t lastExplosion = 0;
    std::cout << "Weapons: 1 pistol, 2 SMG, 3 shotgun, 4 sniper, 5 HE grenade; R reload\n" << app.weapons.status() << '\n';
    renderer.setTreeCulling(options.treeCulling);
    renderer.setZombieCulling(options.zombieCulling);
    renderer.setZombieDistanceDetail(options.zombieDistanceDetail);
    renderer.setZombieDrawDistance(options.zombieDrawDistance.value_or(options.zombieView ? 0.0f : 180.0f));
    renderer.setZombieShadows(options.zombieShadows);
    renderer.setSceneCulling(options.sceneCulling);
    renderer.setRoofBatching(options.roofBatching);
    renderer.setWallBatching(options.wallBatching);
    renderer.setShadows(options.shadows);
    renderer.setFlashlightShadows(options.flashlightShadows);
    renderer.setTreeShadows(options.treeShadows);
    renderer.setTreeCanopyShading(options.treeCanopyShading);
    renderer.setTreeDepthOrdering(options.treeDepthOrdering);
    renderer.setTreeCardSplitting(options.treeCardSplitting);
    renderer.setTreeNearestFiltering(options.treeNearestFiltering);
    renderer.setSunShadowResolution(options.sunShadowResolution);
    renderer.setTreeBillboardDistance(options.treeBillboardDistance);
    renderer.setOldForestDistance(options.oldForestDistance);
    renderer.setNight(options.night);
    renderer.setRain(options.rain);
    renderer.setHaze(options.haze);
    renderer.setGroundFog(options.groundFog);
    viewer::Atmosphere atmosphere;
    bool fogAnchored = false;
    const auto anchorGroundFog = [&] {
        if (!fogAnchored && app.rig.walking()) {
            atmosphere.groundFog.baseHeight = app.rig.player().feet().y;
            renderer.setAtmosphere(atmosphere);
            fogAnchored = true;
        }
    };
    anchorGroundFog();
    double displayedHour = options.dayNight.startHour;
    if (!options.night) {
        renderer.setDayNightLighting(viewer::dayNightLighting(displayedHour, {}, {}, atmosphere));
        std::cout << "Day/night: starting hour " << displayedHour << ", full day "
                  << options.dayNight.dayLengthSeconds << " simulation seconds (0 freezes time)\n";
    }
    std::unique_ptr<viewer::CollisionDebug> debug;
    if (options.selfTest) inputSelfTest(window.get());
    if (options.selfTestFPS) fpsInputSelfTest(window.get());
    if (options.selfTest) {
        renderer.resize(800, 600);
        renderer.render(app.rig.camera());
        for (const auto mode : {viewer::Camera::ViewMode::original, viewer::Camera::ViewMode::zoom10,
                                viewer::Camera::ViewMode::zoom1})
            reticle.draw(mode, 800, 600);
        viewer::Renderer::checkErrors("resized render target and reticle modes");
    }
    renderer.resize(app.width, app.height);
    viewer::GpuTimer gpuTimer;
    double previous = glfwGetTime(), titleTime = previous;
    auto& simulationClock = viewer::SimulationClock::instance();
    simulationClock.reset();
    double intervalRenderSeconds = 0.0;
    double intervalSimulationSeconds = 0.0;
    double intervalBehaviorSeconds = 0.0;
    double intervalPreparationSeconds = 0.0;
    int frameCount = 0, intervalFrames = 0;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        renderer.setFlashlight(app.flashlight);
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::clamp(now - previous, 0.0, 0.1));
        previous = now;
        if (app.width <= 0 || app.height <= 0) {
            audio.service();
            glfwWaitEventsTimeout(0.05);
            titleTime = glfwGetTime();
            intervalFrames = 0;
            intervalRenderSeconds = 0.0;
            intervalSimulationSeconds = intervalBehaviorSeconds = intervalPreparationSeconds = 0.0;
            gpuTimer.takeAverageMilliseconds();
            continue;
        }
        const double simulationStart = glfwGetTime();
        simulationClock.advance(dt);
        if (!options.night) {
            displayedHour = options.dayNight.hourAt(simulationClock.seconds());
            renderer.setDayNightLighting(viewer::dayNightLighting(displayedHour, {}, {}, atmosphere));
        }
        doors.activateNear(app.rig.camera().position());
        for (std::size_t i = 0; i < boxes.count(); ++i)
            doors.activateNear(boxes.pose(i).center, 8);
        if (zombies)
            for (const auto id : zombies->ragdollIds())
                doors.activateNear(glm::vec3(zombies->instances[id].transform[3]));
        physicsScene.prepare(boxes);
        grenades.prepare(physicsScene, doors);
        if (zombies) zombieDoorCollision.prepare(doors, *zombies);
        physics->step(dt);
        grenades.update(simulationClock.seconds(), collision, zombies ? &*zombies : nullptr, app.displayedAnimationSeconds);
        for (const auto& explosion : grenades.explosions(simulationClock.seconds())) {
            if (explosion.id <= lastExplosion) continue;
            audio.explosion(explosion.position);
            lastExplosion = explosion.id;
        }
        if (zombies) zombies->synchronizePhysics();
        const auto& movingPlayer = app.rig.player();
        const viewer::UndergrowthMotion shrubBefore{movingPlayer.feet(), movingPlayer.bodyHeight(),
            movingPlayer.config().radius, movingPlayer.grounded(), false};
        moveCamera(app, dt);
        std::optional<viewer::UndergrowthEvent> shrubEvent;
        if (app.rig.walking()) {
            const bool running = movingPlayer.stance() == viewer::PlayerStance::Standing &&
                (app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]);
            shrubEvent = undergrowthEmitter.update(dt, shrubBefore,
                {movingPlayer.feet(), movingPlayer.bodyHeight(), movingPlayer.config().radius, movingPlayer.grounded(), running},
                undergrowth);
        } else undergrowthEmitter.reset();
        anchorGroundFog();
        app.displayedAnimationSeconds = options.animationTime.value_or(simulationClock.seconds());
        const double behaviorStart = glfwGetTime();
        if (zombies) {
            const auto& player = app.rig.player();
            const auto& config = player.config();
            const float eyeHeight = app.rig.walking() ? player.eyeHeight() : config.eyeHeight;
            const float height = app.rig.walking() ? player.bodyHeight() : config.height;
            const auto feet = app.rig.walking() ? player.feet() :
                app.rig.camera().position() - glm::vec3(0, eyeHeight, 0);
            zombies->updateBehavior(dt, {feet, eyeHeight, height}, collision,
                                    app.displayedAnimationSeconds);
            if (app.rig.walking())
                for (const auto& noise : app.rig.player().noises())
                    zombies->makeNoise(noise.position, noise.decibels);
        }
        if (updateWeapons(app, simulationClock.seconds())) {
            muzzleSlot = app.weapons.slot();
            muzzleModelPosition = app.weapons.equipped().config.muzzlePosition;
            muzzleFlash.fire(simulationClock.seconds(), {});
        }
        const auto viewCamera = app.weaponMotion.aimCamera(app.rig.camera(), app.weapons.slot(),
            simulationClock.seconds(), app.interaction.mode() == viewer::ControlMode::Combat);
        auto flash = muzzleFlash.light(simulationClock.seconds());
        flash.position = weaponOverlay.muzzlePosition(muzzleSlot, muzzleModelPosition,
            app.weaponMotion.pose(muzzleSlot, simulationClock.seconds()));
        renderer.setMuzzleFlash(flash.transformed(glm::inverse(viewCamera.view())));
        if (shrubEvent) {
            if (zombies) zombies->makeNoise(shrubEvent->noise.position, shrubEvent->noise.decibels);
            audio.rustle(*shrubEvent);
        }
        audio.update(app.rig, zombies ? &*zombies : nullptr, dt, &doors);
        const double preparationStart = glfwGetTime();
        intervalBehaviorSeconds += preparationStart - behaviorStart;
        intervalSimulationSeconds += preparationStart - simulationStart;
        renderer.updateDoors(doors);
        renderer.updateBoxes(boxes);
        renderer.updateGrenades(grenades.poses());
        renderer.updateVegetationBlasts(grenades.explosions(simulationClock.seconds()));
        renderer.setExplosionLights(grenades.lights(simulationClock.seconds()));
        renderer.resize(app.width, app.height);
        if (zombies) renderer.updateZombies(*zombies);
        std::vector<viewer::DebugVertex> overlay;
        if (app.collisionDebug) {
            if (!debug) debug = std::make_unique<viewer::CollisionDebug>(options.shaders);
            overlay = collisionOverlay(app);
        }
        intervalPreparationSeconds += glfwGetTime() - preparationStart;
        gpuTimer.begin();
        const double renderStart = glfwGetTime();
        renderer.render(viewCamera, app.displayedAnimationSeconds);
        if (app.collisionDebug)
            debug->draw(viewCamera, overlay, app.width, app.height);
        const auto& heldWeapon = app.weapons.equipped();
        if (app.interaction.mode() == viewer::ControlMode::Combat &&
            app.rig.camera().viewMode() == viewer::Camera::ViewMode::original &&
            viewer::weaponViewVisible(heldWeapon.config, heldWeapon.loaded, simulationClock.seconds() < heldWeapon.readyAt))
            weaponOverlay.draw(app.weapons.slot(), app.width, app.height, flash,
                               app.weaponMotion.pose(app.weapons.slot(), simulationClock.seconds()));
        reticle.draw(app.rig.camera().viewMode(), app.width, app.height);
        intervalRenderSeconds += glfwGetTime() - renderStart;
        gpuTimer.end();
        // Presentation can wait for VSync, so keep it outside both render timers.
        if (!options.hidden) {
            renderer.present(app.width, app.height);
            glfwSwapBuffers(window.get());
        } else glFlush();
        if (frameCount == 0) viewer::Renderer::checkErrors("first rendered frame");
#ifndef NDEBUG
        viewer::Renderer::checkErrors("render loop");
#endif
        ++frameCount;
        ++intervalFrames;
        const double completed = glfwGetTime();
        const double intervalSeconds = completed - titleTime;
        if (intervalSeconds >= 1.0 && (!options.hidden || options.profile)) {
            const auto fps = static_cast<int>(static_cast<double>(intervalFrames) / intervalSeconds);
            const auto milliseconds = [](double value) {
                std::ostringstream text;
                text << std::fixed << std::setprecision(2) << value << " ms";
                return text.str();
            };
            const auto gpuMilliseconds = gpuTimer.takeAverageMilliseconds();
            const auto gpuTime = gpuMilliseconds ? milliseconds(*gpuMilliseconds)
                : gpuTimer.supported() ? std::string("pending") : std::string("unavailable");
            const double frameScale = 1000.0 / static_cast<double>(intervalFrames);
            const auto cpuTime = milliseconds(intervalRenderSeconds * frameScale);
            const auto simulationTime = milliseconds(intervalSimulationSeconds * frameScale);
            const auto status = app.rig.walking() ? (app.rig.player().grounded() ? " | grounded" : " | airborne") : "";
            const auto& treeStats = renderer.treeStats();
            const auto& sceneStats = renderer.sceneStats();
            const auto treeStatus = trees ? " | Trees " + std::to_string(treeStats.visible) + "/" + std::to_string(treeStats.total) : "";
            const auto townStatus = " | Town " + std::to_string(sceneStats.visible) + "/" + std::to_string(sceneStats.total)
                + " (" + std::to_string(sceneStats.drawCalls) + " draws)";
            const auto zombieStatus = zombies ? " | Zombies " + std::to_string(renderer.zombieVisibleCount()) + "/" +
                std::to_string(renderer.zombieCount()) +
                " (" + std::to_string(zombies->ragdollCount()) + " ragdolls)" : "";
            std::ostringstream clockStatus;
            if (!options.night) {
                const int minutes = static_cast<int>(displayedHour * 60.0);
                clockStatus << " | " << std::setfill('0') << std::setw(2) << minutes / 60
                            << ':' << std::setw(2) << minutes % 60;
            }
            std::ostringstream speedStatus;
            speedStatus << std::fixed << std::setprecision(2) << app.rig.speed(
                app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]);
            const auto caption = title + " | " + app.rig.modeName() + " | " + app.interaction.modeName()
                + " | " + app.weapons.status()
                + (app.interaction.grabbing() ? " (holding)" : "") + status + " | " + std::to_string(fps)
                + " FPS | GPU " + gpuTime + " | CPU render " + cpuTime + " | CPU sim " + simulationTime + townStatus + treeStatus + zombieStatus + clockStatus.str() + " | " + speedStatus.str() + " m/s";
            glfwSetWindowTitle(window.get(), caption.c_str());
            if (options.profile) {
                std::cout << caption << '\n'
                          << "CPU stages: behavior " << milliseconds(intervalBehaviorSeconds * frameScale)
                          << "; preparation " << milliseconds(intervalPreparationSeconds * frameScale);
                if (zombies) {
                    const auto& stats = zombies->behaviorStats();
                    std::cout << "; active zombies " << zombies->behaviors.entries().size()
                              << "; last step " << stats.perceptionChecks << " perception checks, "
                              << stats.sightRays << " sight rays";
                    const auto& crowdStats = zombies->crowdStats();
                    std::cout << "; crowd " << crowdStats.neighborChecks << " neighbor checks, "
                              << crowdStats.budgetStops << " budget stops";
                    const auto& updates = renderer.zombieUpdateStats();
                    std::cout << "; render preparation " << updates.instances << " instances, " << updates.bounds
                              << " bounds" << (updates.full ? " (full refresh)" : " (incremental)");
                }
                std::cout << '\n';
            }
            titleTime = completed;
            intervalFrames = 0;
            intervalRenderSeconds = 0.0;
            intervalSimulationSeconds = intervalBehaviorSeconds = intervalPreparationSeconds = 0.0;
        }
        if (options.frames > 0 && frameCount >= options.frames) break;
    }
    if (!options.screenshot.empty()) renderer.writeScreenshot(options.screenshot);
    if (trees) {
        const auto& stats = renderer.treeStats();
        std::cout << "Last tree frame: " << stats.visible << '/' << stats.total << " visible, "
                  << stats.drawCalls << " instanced draw calls\n";
    }
    if (forests) {
        const auto& stats = renderer.forestStats();
        std::cout << "Last forest frame: " << stats.volumes << " visible volumes, "
                  << stats.drawCalls << " material batches, "
                  << stats.detailedPatches << '/' << stats.patches << " patches using individual trees\n";
    }
    const auto& sceneStats = renderer.sceneStats();
    std::cout << "Last town frame: " << sceneStats.visible << '/' << sceneStats.total << " visible, "
              << sceneStats.drawCalls << " draw calls\n";
    if (zombies) std::cout << "Last zombie frame: " << renderer.zombieVisibleCount() << '/' << renderer.zombieCount()
                          << " visible instances, "
                          << zombies->ragdollCount() << " ragdolls, " << renderer.zombieDrawCalls()
                          << " instanced draw calls\n";
    if (zombies) {
        const auto& stats = renderer.zombieRenderStats();
        std::cout << "Zombie detail: camera LOD0/1/2/3 " << stats.cameraLevels[0] << '/' << stats.cameraLevels[1]
                  << '/' << stats.cameraLevels[2] << '/' << stats.cameraLevels[3]
                  << "; " << stats.cameraTriangles << " camera triangles, " << stats.shadowTriangles
                  << " sun shadow triangles, " << stats.flashlightTriangles << " flashlight triangles; " << stats.distanceCulled << " distance culled\n";
    }
    viewer::Renderer::checkErrors("shutdown");
    std::cout << "Rendered " << frameCount << " frames successfully\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    try {
        const auto options = parseOptions(argc, argv);
        if (options.help) { printHelp(); return 0; }
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << "godollo_viewer: " << error.what() << '\n';
        return 1;
    }
}
