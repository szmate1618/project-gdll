#include "flashlight.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
FlashlightView flashlightView(const Camera& camera, const NightLighting& lighting) {
    const auto forward = camera.direction();
    const auto right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const auto up = glm::normalize(glm::cross(right, forward));
    FlashlightView result;
    // A light coincident with the eye only shadows surfaces already occluded
    // from that eye. A small held-light offset makes its shadows visible.
    result.position = camera.position() + right * .25f - up * .15f + forward * .1f;
    result.direction = glm::normalize(camera.position() + forward * 10.0f - result.position);
    result.view = glm::lookAt(result.position, result.position + result.direction, up);
    result.projection = glm::perspective(glm::radians(lighting.outerAngle * 2), 1.0f,
        NightLighting::flashlightShadowNear, NightLighting::flashlightShadowDistance);
    return result;
}
} // namespace viewer
