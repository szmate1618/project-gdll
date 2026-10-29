#pragma once
#include "display.hpp"
struct GLFWwindow;

namespace multiplayer {
// Initializes GLFW and creates the application window. The owner destroys the
// window and terminates GLFW after its GL resources have been released.
GLFWwindow* createClientWindow(const ClientDisplay& display, bool hidden);
} // namespace multiplayer
