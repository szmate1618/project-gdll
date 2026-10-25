#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <glm/vec3.hpp>

namespace viewer {
class PlayerInventory;
struct MinimapSource;
struct Model;
enum class HudScreen { Gameplay, Inventory, Skills, Health, Map };

// Presentation snapshot only; callers retain simulation/network ownership.
struct HudState {
    HudScreen screen = HudScreen::Gameplay;
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
// in framebuffer pixels, preserving GL state.
class Hud {
public:
    Hud(const std::filesystem::path& directory, PlayerInventory& inventory); // Borrows inventory until destruction.
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;
    void draw(const HudState& state, int width, int height);
    // Queue inventory input on the GL thread. Window fractions also work with
    // HiDPI displays and a render resolution different from the window size.
    void pointerMove(float normalizedX, float normalizedY);
    void pointerButton(int button, bool pressed); // 0: left, 1: right/cancel.
    void mouseWheel(float notches);
    void cancelInventoryDrag();
    // Borrow the terrain's uploaded OpenGL texture; its renderer must outlive
    // this HUD. The mapping is copied; no extra image decode/upload is needed.
    void setMinimap(const MinimapSource& source, unsigned int texture);
    // Snapshot and upload loaded road surfaces once; the model need not outlive the HUD.
    void setMinimapRoads(const Model& model);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
