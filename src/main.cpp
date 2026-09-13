#include "renderer.hpp"
#include "camera_rig.hpp"
#include "collision_debug.hpp"
#include "gpu_timer.hpp"
#include "tree_layer.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
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
    int frames = 0;
    bool hidden = false, allowSoftware = false, selfTest = false, help = false;
    bool fps = false, collisionDebug = false, selfTestFPS = false;
    bool noTrees = false, treeCulling = true, treeCollisions = true;
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
        else if (argument == "--hidden") options.hidden = true;
        else if (argument == "--allow-software") options.allowSoftware = true;
        else if (argument == "--self-test-input") options.selfTest = true;
        else if (argument == "--fps") options.fps = true;
        else if (argument == "--collision-debug") options.collisionDebug = true;
        else if (argument == "--self-test-fps") options.selfTestFPS = true;
        else if (argument == "--trees") options.trees = value();
        else if (argument == "--no-trees") options.noTrees = true;
        else if (argument == "--no-tree-culling") options.treeCulling = false;
        else if (argument == "--no-tree-collisions") options.treeCollisions = false;
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
    return options;
}

void printHelp() {
    std::cout << "Usage: godollo_viewer [model.glb|model.gltf] [options]\n"
              << "Default model: " << VIEWER_DEFAULT_ASSET << "\n\n"
              << "W/S forward/back, A/D strafe, Q/E down/up, mouse look, Shift faster\n"
              << "Tab release/capture mouse, wheel change speed, F frame model, Escape quit\n\n"
              << "F1 free-fly, F2 FPS walking, F3 collision overlay; Space jump in FPS\n\n"
              << "  --fps               Start in walking mode\n"
              << "  --spawn X Z         Start walking near world X/Z in meters\n"
              << "  --collision-debug   Show colliders and ground normal (F3)\n"
              << "  --trees FILE        Load a tree instance JSON explicitly\n"
              << "  --no-trees          Disable automatic adjacent trees.instances.json\n"
              << "  --no-tree-culling   Render all tree instances for comparison\n"
              << "  --no-tree-collisions Disable the estimated trunk colliders\n"
              << "  --self-test-fps     Exercise mode-switch and jump callbacks\n"
              << "  --frames N          Render N frames, then exit\n"
              << "  --screenshot FILE   Save a PPM image; defaults to 3 frames\n"
              << "  --hidden            Hide the window for capture (display still required)\n"
              << "  --allow-software    Permit a CPU OpenGL driver for diagnostics\n"
              << "  --self-test-input   Exercise registered input/resize callbacks\n"
              << "  --shader-dir DIR    Load basic.vert/basic.frag from this directory\n";
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
    const viewer::CollisionWorld& world;
    glm::vec3 boundsMin, boundsMax;
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    int width = 1280, height = 720;
    bool capture = true, firstMouse = true, hidden = false;
    bool collisionDebug = false;
    double lastX = 0, lastY = 0;
};

AppState& state(GLFWwindow* window) { return *static_cast<AppState*>(glfwGetWindowUserPointer(window)); }

void captureMouse(GLFWwindow* window, bool captured) {
    auto& app = state(window);
    app.capture = captured;
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
    else if (key == GLFW_KEY_TAB) captureMouse(window, !app.capture);
    else if (key == GLFW_KEY_F)
        app.rig.frame(static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
    else if (key == GLFW_KEY_F1) app.rig.enterFreeFly();
    else if (key == GLFW_KEY_F2) {
        try { app.rig.enterWalking(); }
        catch (const std::exception& error) { std::cerr << "FPS placement failed: " << error.what() << '\n'; }
    } else if (key == GLFW_KEY_F3) {
        app.collisionDebug = !app.collisionDebug;
        std::cout << "Collision overlay: " << (app.collisionDebug ? "on" : "off") << '\n';
    }
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto& app = state(window);
    if (!app.capture) return;
    if (!app.firstMouse && std::isfinite(x) && std::isfinite(y))
        app.rig.camera().look(static_cast<float>(x - app.lastX), static_cast<float>(app.lastY - y));
    app.lastX = x;
    app.lastY = y;
    app.firstMouse = false;
}

void scrollCallback(GLFWwindow* window, double, double y) {
    auto& rig = state(window).rig;
    if (!rig.walking()) rig.camera().adjustSpeed(std::pow(1.2f, static_cast<float>(std::clamp(y, -20.0, 20.0))));
}

void focusCallback(GLFWwindow* window, int focused) {
    if (!focused) {
        state(window).keys.fill(false);
        state(window).firstMouse = true;
    }
}

void moveCamera(AppState& app, float dt) {
    const auto held = [&](int key) { return app.keys[static_cast<std::size_t>(key)] ? 1.0f : 0.0f; };
    app.rig.update(dt, held(GLFW_KEY_W) - held(GLFW_KEY_S), held(GLFW_KEY_D) - held(GLFW_KEY_A),
                   held(GLFW_KEY_E) - held(GLFW_KEY_Q),
                   app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT], app.keys[GLFW_KEY_SPACE]);
}

void inputSelfTest(GLFWwindow* window) {
    auto& app = state(window);
    // Retrieve the installed callbacks, so this tests the same input path as
    // GLFW events, rather than separate synthetic camera movement code.
    const auto key = glfwSetKeyCallback(window, keyCallback);
    const auto cursor = glfwSetCursorPosCallback(window, cursorCallback);
    const auto resize = glfwSetFramebufferSizeCallback(window, framebufferCallback);
    if (!key || !cursor || !resize) throw std::runtime_error("Input callbacks were not registered");
    const auto original = app.rig.camera();
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
    key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    if (app.capture) throw std::runtime_error("Mouse release self-test failed");
    key(window, GLFW_KEY_TAB, 0, GLFW_RELEASE, 0);
    key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_TAB, 0, GLFW_RELEASE, 0);
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
    if (wasWalking && !app.rig.enterWalking()) throw std::runtime_error("Failed to restore walking mode after input test");
    app.firstMouse = true;
    std::cout << "Input self-test passed: WASDQE movement, mouse look/capture, resize, Escape\n";
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
    std::cout << "FPS input self-test passed: F1/F2 modes, 1.70 m eye height, Space jump/landing, F3 overlay\n";
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
        for (float y : {config.radius, config.height - config.radius}) {
            line(feet + glm::vec3(config.radius * std::cos(a), y, config.radius * std::sin(a)),
                 feet + glm::vec3(config.radius * std::cos(b), y, config.radius * std::sin(b)), color);
        }
        // Two perpendicular great circles show the rounded capsule caps.
        for (int axis = 0; axis < 2; ++axis) {
            const auto capPoint = [&](float angle) {
                const float y = std::sin(angle);
                glm::vec3 p(0, y * config.radius + (y > 0 ? config.height - config.radius : config.radius), 0);
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
    const auto model = viewer::loadModel(options.model);
    std::size_t triangles = 0;
    for (const auto& draw : model.draws) triangles += model.primitives.at(draw.primitive).indices.size() / 3;
    std::cout << "Loaded " << model.primitives.size() << " primitives, " << model.draws.size()
              << " draw instances, " << triangles << " triangles, " << model.images.size() << " images\n";
    for (const auto& warning : model.warnings) std::cerr << "Model warning: " << warning << '\n';
    std::optional<viewer::TreeLayer> trees;
    if (!options.noTrees) {
        const bool explicitTrees = !options.trees.empty();
        const auto path = explicitTrees ? options.trees : options.model.parent_path() / "trees.instances.json";
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
    std::vector<viewer::TrunkCollider> trunks;
    if (trees && options.treeCollisions) {
        trunks.reserve(trees->instances.size());
        for (const auto& tree : trees->instances) {
            const auto height = tree.dimensions.y;
            const auto radius = std::min(height * .5f,
                glm::clamp(.035f * std::min(tree.dimensions.x, tree.dimensions.z), .10f, .50f));
            trunks.push_back({glm::vec3(tree.transform[3]), radius, height});
        }
    }
    // Only the town geometry and simple trunks participate in collision.
    // Shared impostor cards stay exclusively in the rendering layer.
    const viewer::CollisionWorld collision(model, trunks);
    const auto& collisionStats = collision.stats();
    std::cout << "Collision world: " << collisionStats.triangles << " triangles, "
              << collisionStats.buildings << " buildings, " << collisionStats.bvhNodes << " BVH nodes, "
              << collisionStats.trunks << " tree trunks\n";
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
    glfwSetWindowUserPointer(window.get(), &app);
    glfwSetFramebufferSizeCallback(window.get(), framebufferCallback);
    glfwSetKeyCallback(window.get(), keyCallback);
    glfwSetCursorPosCallback(window.get(), cursorCallback);
    glfwSetScrollCallback(window.get(), scrollCallback);
    glfwSetWindowFocusCallback(window.get(), focusCallback);
    captureMouse(window.get(), true);
    if (!options.hidden && glfwRawMouseMotionSupported()) glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    std::cout << "Camera mode: " << app.rig.modeName() << "; speed: " << app.rig.speed()
              << " m/s; F1 free-fly, F2 walk, F3 collisions, WASD move, Space jump, mouse look, Escape quit\n";
    viewer::Renderer renderer(model, options.shaders, trees ? &*trees : nullptr);
    renderer.setTreeCulling(options.treeCulling);
    std::unique_ptr<viewer::CollisionDebug> debug;
    if (options.selfTest) inputSelfTest(window.get());
    if (options.selfTestFPS) fpsInputSelfTest(window.get());
    if (options.selfTest) {
        renderer.resize(800, 600);
        renderer.render(app.rig.camera());
        viewer::Renderer::checkErrors("resized render target");
    }
    renderer.resize(app.width, app.height);
    viewer::GpuTimer gpuTimer;
    double previous = glfwGetTime(), titleTime = previous;
    double intervalRenderSeconds = 0.0;
    int frameCount = 0, intervalFrames = 0;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::clamp(now - previous, 0.0, 0.1));
        previous = now;
        if (app.width <= 0 || app.height <= 0) {
            glfwWaitEventsTimeout(0.05);
            titleTime = glfwGetTime();
            intervalFrames = 0;
            intervalRenderSeconds = 0.0;
            gpuTimer.takeAverageMilliseconds();
            continue;
        }
        moveCamera(app, dt);
        renderer.resize(app.width, app.height);
        std::vector<viewer::DebugVertex> overlay;
        if (app.collisionDebug) {
            if (!debug) debug = std::make_unique<viewer::CollisionDebug>(options.shaders);
            overlay = collisionOverlay(app);
        }
        gpuTimer.begin();
        const double renderStart = glfwGetTime();
        renderer.render(app.rig.camera());
        if (app.collisionDebug)
            debug->draw(app.rig.camera(), overlay, app.width, app.height);
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
        if (intervalSeconds >= 1.0 && !options.hidden) {
            const auto fps = static_cast<int>(static_cast<double>(intervalFrames) / intervalSeconds);
            const auto milliseconds = [](double value) {
                std::ostringstream text;
                text << std::fixed << std::setprecision(2) << value << " ms";
                return text.str();
            };
            const auto gpuMilliseconds = gpuTimer.takeAverageMilliseconds();
            const auto gpuTime = gpuMilliseconds ? milliseconds(*gpuMilliseconds)
                : gpuTimer.supported() ? std::string("pending") : std::string("unavailable");
            const auto cpuTime = milliseconds(intervalRenderSeconds * 1000.0 / static_cast<double>(intervalFrames));
            const auto status = app.rig.walking() ? (app.rig.player().grounded() ? " | grounded" : " | airborne") : "";
            const auto& treeStats = renderer.treeStats();
            const auto treeStatus = trees ? " | Trees " + std::to_string(treeStats.visible) + "/" + std::to_string(treeStats.total) : "";
            const auto caption = title + " | " + app.rig.modeName() + status + " | " + std::to_string(fps)
                + " FPS | GPU " + gpuTime + " | CPU render " + cpuTime + treeStatus + " | " + std::to_string(static_cast<int>(app.rig.speed(
                    app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]))) + " m/s";
            glfwSetWindowTitle(window.get(), caption.c_str());
            titleTime = completed;
            intervalFrames = 0;
            intervalRenderSeconds = 0.0;
        }
        if (options.frames > 0 && frameCount >= options.frames) break;
    }
    if (!options.screenshot.empty()) renderer.writeScreenshot(options.screenshot);
    if (trees) {
        const auto& stats = renderer.treeStats();
        std::cout << "Last tree frame: " << stats.visible << '/' << stats.total << " visible, "
                  << stats.drawCalls << " instanced draw calls\n";
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
