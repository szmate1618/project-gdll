#include "signals.hpp"
#include <algorithm>
#include <cmath>

namespace multiplayer {
std::optional<glm::vec3> signalRayTarget(const viewer::CollisionWorld& collision,
                                       const viewer::Camera& camera) {
    const auto origin = camera.position(), direction = camera.direction();
    if (collision.lineOfSight(origin, origin + direction * signalReach)) return std::nullopt;
    // Visibility of ray prefixes is monotonic. Reuse the accelerated scene,
    // trunk, door and prop query, resolving the first obstruction to < 1 mm.
    float clear = 0, blocked = signalReach;
    for (int i = 0; i < 20; ++i) {
        const float middle = (clear + blocked) * .5f;
        if (collision.lineOfSight(origin, origin + direction * middle)) clear = middle;
        else blocked = middle;
    }
    return origin + direction * clear;
}
SignalPlacement signalPlacement(const viewer::Camera& camera, glm::vec3 position,
                                 int width, int height, float marginPixels) {
    if (width <= 0 || height <= 0) return {position, false};
    const auto view = camera.view();
    const auto projection = camera.projection(static_cast<float>(width) / height);
    const auto p = glm::vec3(view * glm::vec4(position, 1));
    const auto inverseProjection = glm::inverse(projection);
    const auto nearPoint = inverseProjection * glm::vec4(0, 0, -1, 1);
    const auto farPoint = inverseProjection * glm::vec4(0, 0, 1, 1);
    const float nearDepth = -nearPoint.z / nearPoint.w * 1.05f;
    const float farDepth = -farPoint.z / farPoint.w * .95f;
    const float bearingDepth = std::max(std::abs(p.z), .001f);
    const float depth = std::clamp(bearingDepth, nearDepth, farDepth);
    glm::vec2 ndc(projection[0][0] * p.x / bearingDepth, projection[1][1] * p.y / bearingDepth);
    const glm::vec2 limit = glm::max(glm::vec2(.1f), glm::vec2(1) -
        glm::vec2(2 * marginPixels / width, 2 * marginPixels / height));
    const float ratio = std::max(std::abs(ndc.x) / limit.x, std::abs(ndc.y) / limit.y);
    const bool atEdge = p.z >= 0 || ratio > 1;
    if (!atEdge && -p.z >= nearDepth && -p.z <= farDepth) return {position, false};
    // Behind targets keep their left/right/up/down bearing. Exactly behind is
    // ambiguous, so use the bottom edge deterministically.
    if (atEdge) {
        if (ratio < 1e-6f) ndc = {0, -limit.y};
        else ndc /= ratio;
    }
    const glm::vec3 adjusted(ndc.x * depth / projection[0][0], ndc.y * depth / projection[1][1], -depth);
    return {glm::vec3(glm::inverse(view) * glm::vec4(adjusted, 1)), atEdge};
}
} // namespace multiplayer
