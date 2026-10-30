#pragma once
#include <array>
#include <filesystem>
#include "opengl.hpp"
#include "weapon_view_pose.hpp"
#include "weapon_motion.hpp"
#include "muzzle_flash.hpp"
#include "weapons.hpp"
#include "camera.hpp"

namespace viewer {
// Shared weapon meshes, without collision or shadow casters. The local overlay
// clears depth for self-occlusion; remote world draws retain scene depth.
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
    // The remote player supplies its eye-to-world transform. Reuse the exact
    // local mount and geometry while testing against the viewing scene's depth.
    void drawWorld(std::size_t slot, const Camera& viewingCamera, float aspect, const glm::mat4& eyeToWorld,
                   const MuzzleFlashLight& worldFlash = {}, const WeaponMotionPose& motion = {});
private:
    void drawGeometry(std::size_t slot, const glm::mat4& projection, const glm::mat4& modelToEye,
                      const MuzzleFlashLight& flash, float pumpTravel);
    struct Range { GLint first = 0; GLsizei count = 0; };
    std::array<Range, weaponIds.size()> ranges_{};
    std::array<glm::mat4, weaponIds.size()> poses_{};
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLint transform_ = -1;
    GLint pose_ = -1, flashPosition_ = -1, flashColor_ = -1, flashRange_ = -1;
    GLint pumpTravel_ = -1;
};
} // namespace viewer
