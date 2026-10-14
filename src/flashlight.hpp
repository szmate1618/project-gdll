#pragma once

#include "camera.hpp"
#include "night_lighting.hpp"

namespace viewer {

// Camera-relative held light, in world meters. Its 44-degree perspective shadow
// volume covers only the nearby cone, independently of camera zoom or sun light.
struct FlashlightView {
    glm::vec3 position{0}, direction{0, 0, -1};
    glm::mat4 view{1}, projection{1};
    glm::mat4 projectionView() const { return projection * view; }
};

FlashlightView flashlightView(const Camera& camera, const NightLighting& lighting = {});

} // namespace viewer
