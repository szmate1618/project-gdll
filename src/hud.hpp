#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <glm/vec3.hpp>

namespace viewer {

// Presentation snapshot only; callers retain simulation/network ownership.
struct HudState {
    glm::vec3 position{0}; // Runtime metres: X east, Y up, Z south.
    float yawDegrees = -90; // Camera yaw; -90 is north, 0 is east.
    float speedMetersPerSecond = 0; // Horizontal travel speed; displayed in km/h.
    std::string movement = "FREE FLY", mode = "COMBAT";
    std::string equipment = "CONNECTING", prompt;
    std::optional<int> loaded, reserve;
    int slot = -1; // Equipment slots 0..4; fists are 5. -1 means unconfirmed.
    bool mouseCaptured = true;
};

// Owns the application's RmlUi lifetime. One HUD at a time, on the GL thread.
// Construct/destroy with a current context. Draws into the currently bound FBO
// in framebuffer pixels, preserving GL state. No image files are loaded.
class Hud {
public:
    explicit Hud(const std::filesystem::path& directory);
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;
    void draw(const HudState& state, int width, int height);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
