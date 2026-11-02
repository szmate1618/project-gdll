#pragma once

#include "opengl.hpp"
#include "camera.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <string>

namespace viewer {
enum class SceneDepthMode { Conventional24, Conventional32, Reversed32 };

// Scoped to the camera color pass. CPU visibility and both shadow-map passes
// retain their conventional [-1,1] matrices and depth comparisons.
inline thread_local bool sceneReversedDepth = false;

inline PFNGLCLIPCONTROLPROC sceneClipControl() {
    GLint major = 0, minor = 0, count = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    bool supported = major > 4 || (major == 4 && minor >= 5);
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; !supported && i < count; ++i)
        supported = std::string(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)))) == "GL_ARB_clip_control";
    return supported ? reinterpret_cast<PFNGLCLIPCONTROLPROC>(glfwGetProcAddress("glClipControl")) : nullptr;
}

inline glm::mat4 sceneProjection(const Camera& camera, float aspect) {
    return camera.projection(aspect, sceneReversedDepth);
}

// Only used for local body/weapon matrices supplied by older callers. Distant
// world rendering builds reversed projection coefficients directly in Camera.
inline glm::mat4 sceneLocalProjection(glm::mat4 matrix) {
    if (sceneReversedDepth)
        for (int column = 0; column < 4; ++column)
            matrix[column][2] = float((double(matrix[column][3]) - double(matrix[column][2])) * .5);
    return matrix;
}

class SceneDepthScope {
public:
    SceneDepthScope(PFNGLCLIPCONTROLPROC clip, bool reversed)
        : clip_(clip), previous_(sceneReversedDepth) { apply(reversed); }
    ~SceneDepthScope() { apply(previous_); }
    SceneDepthScope(const SceneDepthScope&) = delete;
    SceneDepthScope& operator=(const SceneDepthScope&) = delete;
private:
    void apply(bool reversed) {
        if (clip_) clip_(GL_LOWER_LEFT, reversed ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
        sceneReversedDepth = reversed;
        glDepthFunc(reversed ? GL_GREATER : GL_LESS);
        glClearDepth(reversed ? 0 : 1);
    }
    PFNGLCLIPCONTROLPROC clip_;
    bool previous_;
};
} // namespace viewer
