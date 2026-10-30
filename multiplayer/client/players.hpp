#pragma once
#include "protocol.hpp"
#include "camera.hpp"
#include "opengl.hpp"
#include "remote_weapon_playback.hpp"
#include <map>

namespace viewer { class WeaponOverlay; }

namespace multiplayer {
class PlayerRenderer {
public:
    // Optional borrowed geometry owner must outlive this renderer.
    explicit PlayerRenderer(viewer::WeaponOverlay* sharedWeapon = nullptr);
    ~PlayerRenderer();
    void setAppearance(std::uint32_t player, const CharacterAppearance& appearance);
    void removeAppearance(std::uint32_t player);
    void receiveShots(const std::vector<SoundEvent>& shots, const std::vector<PlayerState>& players,
                      double serverSeconds);
    std::vector<viewer::MuzzleFlashLight> muzzleLights(double serverSeconds) const;
    void draw(const viewer::Camera& camera, const std::vector<PlayerState>& players,
              std::uint32_t self, int width, int height, double serverSeconds = 0);
private:
    viewer::WeaponOverlay* weapon_ = nullptr;
    RemoteWeaponPlayback weapons_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint matrix_ = -1, color_ = -1, textured_ = -1;
    struct AppearanceTexture { GLuint texture = 0; glm::vec3 base{1}; };
    std::map<std::uint32_t, AppearanceTexture> appearances_;
};
} // namespace multiplayer
