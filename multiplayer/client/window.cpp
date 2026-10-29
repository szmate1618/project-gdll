#include "window.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace multiplayer {
GLFWwindow* createClientWindow(const ClientDisplay& display, bool hidden) {
    if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; --headless is available for network checks");
    try {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
        auto size = display.resolution.value_or(glm::ivec2(1280, 720));
        GLFWmonitor* monitor = nullptr;
        if (display.fullscreen) {
            auto* primary = glfwGetPrimaryMonitor();
            const auto* mode = primary ? glfwGetVideoMode(primary) : nullptr;
            if (!mode) throw std::runtime_error("Cannot find a monitor for fullscreen mode");
            size = {mode->width, mode->height};
            // Keep the desktop mode; scale the separate 3D target into it.
            // Hidden captures never occupy the monitor or change its mode.
            if (!hidden) monitor = primary;
            glfwWindowHint(GLFW_RED_BITS, mode->redBits);
            glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        }
        auto* window = glfwCreateWindow(size.x, size.y, "Godollo", monitor, nullptr);
        if (!window) throw std::runtime_error("Cannot create OpenGL 3.3 window");
        return window;
    } catch (...) {
        glfwTerminate();
        throw;
    }
}
} // namespace multiplayer
