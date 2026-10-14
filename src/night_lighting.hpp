#pragma once

#include <glm/glm.hpp>

namespace viewer {

// Linear RGB lighting for night mode. The flashlight is held near the eyes
// and follows camera aim. Distances are meters; cone angles are degrees from
// its axis. Tree cards receive the beam without using their billboard normals.
struct NightLighting {
    glm::vec3 skyAmbient{0.012f, 0.018f, 0.030f};
    glm::vec3 groundAmbient{0.006f, 0.008f, 0.012f};
    glm::vec3 fogColor{0.0018f, 0.0030f, 0.0060f};
    glm::vec3 flashlightColor{8.0f, 7.6f, 6.8f};
    float flashlightRange = 40.0f;
    static constexpr int flashlightShadowResolution = 1024;
    static constexpr float flashlightShadowNear = 0.05f;
    static constexpr float flashlightShadowDistance = 25.0f;
    static constexpr float flashlightShadowFadeStart = 20.0f;
    float innerAngle = 12.0f;
    float outerAngle = 22.0f;
};

} // namespace viewer
