#include "sun_lighting.hpp"

#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {

SunShadowView sunShadowView(glm::vec3 eye, glm::vec3 sunDirection) {
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(eye[axis]) || !std::isfinite(sunDirection[axis]))
            throw std::invalid_argument("Sun shadow view requires finite coordinates");
    const float length = glm::length(sunDirection);
    if (!std::isfinite(length) || length < 0.0001f)
        throw std::invalid_argument("Sun direction must be nonzero");
    const auto sun = sunDirection / length;
    const auto up = std::abs(sun.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const auto rotation = glm::lookAt(glm::vec3(0), -sun, up);
    // Padding protects the fading receiver boundary and PCF footprint.
    constexpr float extent = SunLighting::shadowDistance + 2.0f;
    constexpr float casterReach = 150.0f;
    constexpr float texel = 2.0f * extent / SunLighting::shadowResolution;
    auto center = glm::vec3(rotation * glm::vec4(eye, 1));
    center.x = std::round(center.x / texel) * texel;
    center.y = std::round(center.y / texel) * texel;
    center.z += extent + casterReach;
    SunShadowView result;
    result.view = glm::translate(glm::mat4(1), -center) * rotation;
    result.projection = glm::ortho(-extent, extent, -extent, extent,
                                  0.1f, 2.0f * extent + casterReach);
    return result;
}

} // namespace viewer
