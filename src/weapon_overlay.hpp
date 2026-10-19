#pragma once
#include <array>
#include <filesystem>
#include "opengl.hpp"
#include "weapon_view_pose.hpp"
#include "weapon_motion.hpp"
#include "muzzle_flash.hpp"
#include "weapons.hpp"

namespace viewer {
// Camera-local meshes, without collision or shadow casters. Draw after scene and
// debug geometry: this final overlay pass clears depth for weapon self-occlusion.
class WeaponOverlay {
public:
    WeaponOverlay(const std::filesystem::path& shaders, const std::filesystem::path& models,
                  WeaponViewBody body = {});
    ~WeaponOverlay();
    WeaponOverlay(const WeaponOverlay&) = delete;
    WeaponOverlay& operator=(const WeaponOverlay&) = delete;
    // Transform the configured model-local muzzle to eye-local meters.
    glm::vec3 muzzlePosition(std::size_t slot, glm::vec3 modelMuzzle, const WeaponMotionPose& motion = {}) const;
    void draw(std::size_t slot, int width, int height, const MuzzleFlashLight& flash = {},
              const WeaponMotionPose& motion = {});
private:
    struct Range { GLint first = 0; GLsizei count = 0; };
    std::array<Range, weaponIds.size()> ranges_{};
    std::array<glm::mat4, weaponIds.size()> poses_{};
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLint transform_ = -1;
    GLint pose_ = -1, flashPosition_ = -1, flashColor_ = -1, flashRange_ = -1;
    GLint pumpTravel_ = -1;
};
} // namespace viewer
