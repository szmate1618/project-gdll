#pragma once

#include <GLFW/glfw3.h>

namespace viewer {
// Raw motion affects captured mouse look only; menus retain normal cursor input.
// Unsupported platforms keep GLFW's default mouse motion.
inline void enableRawMouseMotion(GLFWwindow* window) {
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}
} // namespace viewer
