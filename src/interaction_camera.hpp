#pragma once

#include "camera.hpp"
#include "presentation_viewport.hpp"
#include <optional>

namespace viewer {
// Window fractions use a top-left origin. Letterbox coordinates have no world
// ray; the scene's projection, rather than the window aspect, determines aim.
inline std::optional<glm::vec3> cursorInteractionDirection(const Camera& camera,
    glm::vec2 pointer, glm::ivec2 sceneSize, glm::ivec2 displaySize) {
    if (sceneSize.x <= 0 || sceneSize.y <= 0 || displaySize.x <= 0 || displaySize.y <= 0 ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) return {};
    const auto viewport = presentationViewport(sceneSize.x,sceneSize.y,displaySize.x,displaySize.y);
    const auto point = viewport.scenePointer(pointer.x,pointer.y,sceneSize.x,sceneSize.y) / glm::vec2(sceneSize);
    if (point.x < 0 || point.x > 1 || point.y < 0 || point.y > 1) return {};
    const auto projection = camera.projection(float(sceneSize.x) / sceneSize.y);
    const auto forward = camera.direction();
    const auto right = glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
    const auto up = glm::cross(right,forward);
    return glm::normalize(forward + right * ((2 * point.x - 1) / projection[0][0]) +
        up * ((1 - 2 * point.y) / projection[1][1]));
}

inline bool validInteractionDirection(glm::vec3 direction) {
    return std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z) &&
        std::abs(glm::length(direction) - 1) <= .001f;
}

// Only orientation changes. The server supplies its authoritative eye position;
// a client cannot move an interaction origin through a wall or across the map.
inline Camera interactionCamera(const Camera& eye, const std::optional<glm::vec3>& direction) {
    if (!direction || !validInteractionDirection(*direction) || glm::dot(*direction,eye.direction()) <= 0) return eye;
    return eye.aimed(*direction);
}
} // namespace viewer
