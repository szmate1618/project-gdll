#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct Options {
    std::filesystem::path model = VIEWER_DEFAULT_ASSET;
    std::filesystem::path shaders = VIEWER_SHADER_DIR;
    std::filesystem::path screenshot;
    int frames = 0;
    bool hidden = false, allowSoftware = false, selfTest = false, help = false;
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
    if ((options.hidden || options.selfTest || !options.screenshot.empty()) && options.frames == 0) options.frames = 3;
    return options;
}

void printHelp() {
    std::cout << "Usage: godollo_viewer [model.glb|model.gltf] [options]\n"
              << "Default model: " << VIEWER_DEFAULT_ASSET << "\n\n"
              << "W/S forward/back, A/D strafe, Q/E down/up, mouse look, Shift faster\n"
              << "Tab release/capture mouse, wheel change speed, F frame model, Escape quit\n\n"
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
    viewer::Camera camera;
    glm::vec3 boundsMin, boundsMax;
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    int width = 1280, height = 720;
    bool capture = true, firstMouse = true, hidden = false;
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
        app.camera.frame(app.boundsMin, app.boundsMax, static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto& app = state(window);
    if (!app.capture) return;
    if (!app.firstMouse && std::isfinite(x) && std::isfinite(y))
        app.camera.look(static_cast<float>(x - app.lastX), static_cast<float>(app.lastY - y));
    app.lastX = x;
    app.lastY = y;
    app.firstMouse = false;
}

void scrollCallback(GLFWwindow* window, double, double y) {
    state(window).camera.adjustSpeed(std::pow(1.2f, static_cast<float>(std::clamp(y, -20.0, 20.0))));
}

void focusCallback(GLFWwindow* window, int focused) {
    if (!focused) {
        state(window).keys.fill(false);
        state(window).firstMouse = true;
    }
}

void moveCamera(AppState& app, float dt) {
    const auto held = [&](int key) { return app.keys[static_cast<std::size_t>(key)] ? 1.0f : 0.0f; };
    app.camera.move(held(GLFW_KEY_W) - held(GLFW_KEY_S), held(GLFW_KEY_D) - held(GLFW_KEY_A),
                    held(GLFW_KEY_E) - held(GLFW_KEY_Q), dt,
                    app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]);
}

void inputSelfTest(GLFWwindow* window) {
    auto& app = state(window);
    // Retrieve the installed callbacks, so this tests the same input path as
    // GLFW events, rather than separate synthetic camera movement code.
    const auto key = glfwSetKeyCallback(window, keyCallback);
    const auto cursor = glfwSetCursorPosCallback(window, cursorCallback);
    const auto resize = glfwSetFramebufferSizeCallback(window, framebufferCallback);
    if (!key || !cursor || !resize) throw std::runtime_error("Input callbacks were not registered");
    const auto original = app.camera;
    for (int code : {GLFW_KEY_W, GLFW_KEY_S, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_Q, GLFW_KEY_E}) {
        const auto before = app.camera.position();
        key(window, code, 0, GLFW_PRESS, 0);
        moveCamera(app, 0.1f);
        key(window, code, 0, GLFW_RELEASE, 0);
        if (glm::length(app.camera.position() - before) < 1e-5f) throw std::runtime_error("Movement input self-test failed");
    }
    const auto before = app.camera.view();
    app.firstMouse = true;
    cursor(window, 100, 100);
    cursor(window, 145, 80);
    float change = 0;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) change += std::abs(app.camera.view()[c][r] - before[c][r]);
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
    app.camera = original;
    app.firstMouse = true;
    std::cout << "Input self-test passed: WASDQE movement, mouse look/capture, resize, Escape\n";
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
    AppState app;
    app.boundsMin = model.boundsMin;
    app.boundsMax = model.boundsMax;
    app.hidden = options.hidden;
    glfwGetFramebufferSize(window.get(), &app.width, &app.height);
    app.camera.frame(model.boundsMin, model.boundsMax, static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
    glfwSetWindowUserPointer(window.get(), &app);
    glfwSetFramebufferSizeCallback(window.get(), framebufferCallback);
    glfwSetKeyCallback(window.get(), keyCallback);
    glfwSetCursorPosCallback(window.get(), cursorCallback);
    glfwSetScrollCallback(window.get(), scrollCallback);
    glfwSetWindowFocusCallback(window.get(), focusCallback);
    captureMouse(window.get(), true);
    if (!options.hidden && glfwRawMouseMotionSupported()) glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    std::cout << "Camera speed: " << app.camera.speed() << " m/s; W/S A/D Q/E, mouse look, Shift faster, Tab cursor, F frame, Escape quit\n";
    viewer::Renderer renderer(model, options.shaders);
    if (options.selfTest) inputSelfTest(window.get());
    renderer.resize(app.width, app.height);
    double previous = glfwGetTime(), titleTime = previous;
    int frameCount = 0, intervalFrames = 0;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::clamp(now - previous, 0.0, 0.1));
        previous = now;
        if (app.width <= 0 || app.height <= 0) { glfwWaitEventsTimeout(0.05); continue; }
        moveCamera(app, dt);
        renderer.resize(app.width, app.height);
        renderer.render(app.camera);
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
        if (now - titleTime >= 1.0 && !options.hidden) {
            const auto fps = static_cast<int>(static_cast<double>(intervalFrames) / (now - titleTime));
            const auto caption = title + " | " + std::to_string(fps) + " FPS | " + std::to_string(static_cast<int>(app.camera.speed())) + " m/s";
            glfwSetWindowTitle(window.get(), caption.c_str());
            titleTime = now;
            intervalFrames = 0;
        }
        if (options.frames > 0 && frameCount >= options.frames) break;
    }
    if (!options.screenshot.empty()) renderer.writeScreenshot(options.screenshot);
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
