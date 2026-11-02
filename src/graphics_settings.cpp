#include "graphics_settings.hpp"
#include "graphics_settings_file.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <utility>

namespace viewer {
RuntimeGraphicsSettings::RuntimeGraphicsSettings(GLFWwindow* window, Renderer& renderer, bool fullscreen,
                                                glm::ivec2 resolution, std::filesystem::path savePath)
    : window_(window), renderer_(renderer), savePath_(std::move(savePath)) {
    settings_.fullscreen = glfwGetWindowAttrib(window_, GLFW_VISIBLE) ? glfwGetWindowMonitor(window_) != nullptr : fullscreen;
    const auto selected = resolution;
    for (std::size_t i = 0; i < renderResolutions.size(); ++i)
        if (renderResolutions[i] == selected) settings_.resolution = static_cast<int>(i);
    settings_.shadowCasters = renderer.shadowCasters();
    settings_.skipHighestDetail = renderer.skipHighestDetail();
    if (renderer.distantSceneCacheEnabled())
        for (std::size_t i = 0; i < distantSceneryDistances.size(); ++i)
            if (distantSceneryDistances[i] == renderer.distantSceneDistance()) settings_.distantScenery = static_cast<int>(i);
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
    validateGraphicsSettings(next);
    if (next.fullscreen != settings_.fullscreen) setFullscreen(next.fullscreen);
    if (next.distantScenery != settings_.distantScenery) {
        renderer_.setDistantSceneDistance(distantSceneryDistances[next.distantScenery]);
        renderer_.setDistantSceneCache(next.distantScenery != 3);
    }
    renderer_.setShadowCasters(next.shadowCasters);
    renderer_.setSkipHighestDetail(next.skipHighestDetail);
    settings_ = next;
    if (!savePath_.empty()) {
        try { saveGraphicsSettings(savePath_, settings_); }
        catch (const std::exception& error) {
            std::cerr << "Graphics settings could not be saved to " << savePath_.u8string() << ": " << error.what() << '\n';
        }
    }
}
void RuntimeGraphicsSettings::syncDistantScenery(bool enabled) {
    if (enabled == (settings_.distantScenery != 3)) return;
    auto next = settings_;
    next.distantScenery = enabled ? 2 : 3;
    apply(next);
}
} // namespace viewer
