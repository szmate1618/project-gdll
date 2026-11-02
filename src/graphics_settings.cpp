#include "graphics_settings.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace viewer {
RuntimeGraphicsSettings::RuntimeGraphicsSettings(GLFWwindow* window, Renderer& renderer, bool fullscreen,
                                                glm::ivec2 resolution)
    : window_(window), renderer_(renderer) {
    settings_.fullscreen = glfwGetWindowAttrib(window_, GLFW_VISIBLE) ? glfwGetWindowMonitor(window_) != nullptr : fullscreen;
    const auto selected = resolution;
    for (std::size_t i = 0; i < renderResolutions.size(); ++i)
        if (renderResolutions[i] == selected) settings_.resolution = static_cast<int>(i);
    settings_.shadowCasters = renderer.shadowCasters();
    settings_.distantScenery = renderer.distantSceneCacheEnabled() ? 2 : 3;
    if (!glfwGetWindowMonitor(window_)) {
        glfwGetWindowPos(window_, &windowPosition_.x, &windowPosition_.y);
        glfwGetWindowSize(window_, &windowSize_.x, &windowSize_.y);
    }
}
glm::ivec2 RuntimeGraphicsSettings::renderSize() const {
    return renderResolutions[settings_.resolution];
}
void RuntimeGraphicsSettings::setFullscreen(bool enabled) {
    // Hidden captures never occupy a monitor or change its desktop mode.
    if (!glfwGetWindowAttrib(window_, GLFW_VISIBLE)) return;
    if (enabled) {
        auto* monitor = glfwGetPrimaryMonitor();
        const auto* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) throw std::runtime_error("Cannot find a monitor for fullscreen mode");
        glfwGetWindowPos(window_, &windowPosition_.x, &windowPosition_.y);
        glfwGetWindowSize(window_, &windowSize_.x, &windowSize_.y);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowPosition_.x, windowPosition_.y,
                             windowSize_.x, windowSize_.y, GLFW_DONT_CARE);
    }
}
void RuntimeGraphicsSettings::apply(const GraphicsSettings& next) {
    if (next.resolution < 0 || next.resolution >= static_cast<int>(renderResolutions.size()) ||
        next.distantScenery < 0 || next.distantScenery >= static_cast<int>(distantSceneryDistances.size()) ||
        next.shadowCasters < ShadowCasters::None || next.shadowCasters > ShadowCasters::Objects)
        throw std::invalid_argument("Invalid graphics settings");
    if (next.fullscreen != settings_.fullscreen) setFullscreen(next.fullscreen);
    if (next.distantScenery != settings_.distantScenery) {
        renderer_.setDistantSceneDistance(distantSceneryDistances[next.distantScenery]);
        renderer_.setDistantSceneCache(next.distantScenery != 3);
    }
    if (next.shadowCasters != settings_.shadowCasters) renderer_.setShadowCasters(next.shadowCasters);
    if (next.skipHighestDetail != settings_.skipHighestDetail) renderer_.setSkipHighestDetail(next.skipHighestDetail);
    settings_ = next;
}
void RuntimeGraphicsSettings::syncDistantScenery(bool enabled) {
    if (enabled == (settings_.distantScenery != 3)) return;
    auto next = settings_;
    next.distantScenery = enabled ? 2 : 3;
    apply(next);
}
} // namespace viewer
