#pragma once

#include <glm/glm.hpp>

namespace viewer {

// Linear RGB intensities. Direction points from the surface toward the sun in
// the runtime frame (X east, Y up, Z south); distances are world-space meters.
struct SunLighting {
    glm::vec3 direction{-0.4f, 0.85f, 0.3f};
    glm::vec3 color{0.78f, 0.74f, 0.66f};
    glm::vec3 skyAmbient{0.25f, 0.29f, 0.35f};
    glm::vec3 groundAmbient{0.13f, 0.12f, 0.10f};
    static constexpr int shadowResolution = 2048;
    static constexpr float shadowDistance = 120.0f;
    static constexpr float shadowFadeStart = 90.0f;
};

struct SunShadowView {
    glm::mat4 view{1};
    glm::mat4 projection{1};
    glm::mat4 projectionView() const { return projection * view; }
};

// Fixed-size coverage independent of camera yaw/FOV. Snap the light's X/Y
// origin to texels to avoid shadow swimming while walking. Include upstream
// casters beyond the receiver sphere, even when outside the camera frustum.
SunShadowView sunShadowView(glm::vec3 eye, glm::vec3 sunDirection);

} // namespace viewer
