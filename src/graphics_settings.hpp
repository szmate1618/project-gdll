#pragma once

#include <array>
#include <glm/vec2.hpp>

struct GLFWwindow;
namespace viewer {
class Renderer;

// Cumulative shadow-caster groups, shared by sunlight and flashlight.
enum class ShadowCasters { None, Buildings, Trees, Npcs, Objects };
inline constexpr std::array<glm::ivec2, 3> renderResolutions{{{960, 540}, {1280, 720}, {1920, 1080}}};
inline constexpr std::array<float, 4> distantSceneryDistances{{500, 750, 1000, 0}}; // Metres; zero disables.
struct GraphicsSettings {
    bool fullscreen = false;
    int resolution = 1, distantScenery = 3;
    ShadowCasters shadowCasters = ShadowCasters::Objects;
    bool skipHighestDetail = false;
};

// Application-thread display/renderer adapter. Preserves window position and
// size across fullscreen changes. GUI pixels always follow the framebuffer.
class RuntimeGraphicsSettings {
public:
    RuntimeGraphicsSettings(GLFWwindow* window, Renderer& renderer, bool fullscreen,
                            glm::ivec2 resolution = {1280, 720});
    const GraphicsSettings& settings() const { return settings_; }
    glm::ivec2 renderSize() const;
    void apply(const GraphicsSettings& settings);
    void syncDistantScenery(bool enabled); // Existing F7 shortcut.
private:
    void setFullscreen(bool enabled);
    GLFWwindow* window_;
    Renderer& renderer_;
    GraphicsSettings settings_;
    glm::ivec2 windowPosition_{100, 100}, windowSize_{1280, 720};
};
} // namespace viewer
