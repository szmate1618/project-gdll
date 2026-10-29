#include "zombie_selection.hpp"

#include "visibility.hpp"
#include "zombie_lod.hpp"
#include "flashlight.hpp"
#include "sun_lighting.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace multiplayer {
namespace {
constexpr float nearbyThreatRadius = 30;

float boxDistanceSquared(const viewer::VisibilityBounds& bounds, glm::vec3 point) {
    const float x = point.x - std::min(std::max(point.x, bounds.minimum.x), bounds.maximum.x);
    const float y = point.y - std::min(std::max(point.y, bounds.minimum.y), bounds.maximum.y);
    const float z = point.z - std::min(std::max(point.z, bounds.minimum.z), bounds.maximum.z);
    return (x * x + y * y) + z * z;
}

float boundsCenterDistance(const viewer::VisibilityBounds& bounds, glm::vec3 point) {
    const float x = point.x - (bounds.minimum.x + bounds.maximum.x) * .5f;
    const float y = point.y - (bounds.minimum.y + bounds.maximum.y) * .5f;
    const float z = point.z - (bounds.minimum.z + bounds.maximum.z) * .5f;
    return std::sqrt((x * x + y * y) + z * z);
}

viewer::Frustum expandedFrustum(const ReplicationView& view, float marginDegrees) {
    const float yaw = glm::radians(view.yaw), pitch = glm::radians(view.pitch);
    const glm::vec3 direction{std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                              std::sin(yaw) * std::cos(pitch)};
    constexpr float near = .1f;
    const float vertical = std::tan(glm::radians(view.verticalFov) * .5f);
    const float margin = std::tan(glm::radians(marginDegrees));
    const float x = near * (vertical * view.aspect + margin);
    const float y = near * (vertical + margin);
    return viewer::Frustum(glm::frustum(-x, x, -y, y, near, 10000.f) *
        glm::lookAt(view.eye, view.eye + direction, glm::vec3(0, 1, 0)));
}

float volumeRadius(const glm::mat4& projectionView, glm::vec3 center) {
    const auto inverse = glm::inverse(projectionView);
    float radius = 0;
    for (int corner = 0; corner < 8; ++corner) {
        auto point = inverse * glm::vec4(corner & 1 ? 1.f : -1.f, corner & 2 ? 1.f : -1.f,
                                         corner & 4 ? 1.f : -1.f, 1);
        point /= point.w;
        radius = std::max(radius, glm::distance(glm::vec3(point), center));
    }
    return radius + 4; // Enclose the 2 m per-axis snap/time padding below.
}
}

void selectZombieInterest(viewer::ZombieLayer& zombies, glm::vec3 feet,
        const std::optional<ReplicationView>& view, const std::vector<std::uint8_t>& previous,
        std::vector<std::size_t>& selected, const std::optional<glm::vec3>& sunDirection) {
    selected.clear();
    selected.reserve(maxVisibleZombies);
    std::optional<viewer::Frustum> entering, retained;
    std::vector<viewer::Frustum> shadows;
    float radius = 240, projectionScale = 1;
    glm::vec3 center = feet;
    if (view) {
        entering.emplace(expandedFrustum(*view, 8));
        retained.emplace(expandedFrustum(*view, 12));
        projectionScale = 1.f / std::tan(glm::radians(view->verticalFov) * .5f);
        radius = viewer::zombieCameraDrawRadius(projectionScale, true, view->maxDistance * 1.15f).value_or(10000);
        center = view->eye;
        radius = std::max(radius + glm::distance(center, feet), nearbyThreatRadius);
        if (sunDirection) {
            const auto matrix = viewer::sunShadowView(view->eye, *sunDirection).projectionView();
            shadows.emplace_back(matrix);
            radius = std::max(radius, volumeRadius(matrix, feet));
        }
        // The light can be toggled locally. Include its potential caster cone
        // even while off so toggling it does not wait for new replication.
        viewer::Camera camera;
        camera.setPosition(view->eye); camera.setOrientation(view->yaw, view->pitch);
        const auto flashlight = viewer::flashlightView(camera).projectionView();
        shadows.emplace_back(flashlight);
        radius = std::max(radius, volumeRadius(flashlight, feet));
    }
    zombies.visitReplicationNear(feet, radius, [&](std::size_t id) {
        const auto& instance = zombies.instances[id];
        if (instance.removed || instance.asset >= zombies.assets.size()) return true;
        const auto& bounds = zombies.replicationBounds(id);
        const bool known = id < previous.size() && previous[id] != 0;
        bool include;
        if (!view) {
            const float distance = known ? 240.f : 220.f;
            include = boxDistanceSquared(bounds, feet) <= distance * distance;
        } else {
            include = boxDistanceSquared(bounds, feet) <= nearbyThreatRadius * nearbyThreatRadius;
            if (!include) {
                const auto& frustum = known ? *retained : *entering;
                const auto detail = viewer::zombieCameraDetail(boundsCenterDistance(bounds, center), projectionScale,
                    true, view->maxDistance * (known ? 1.15f : 1.05f));
                include = detail.visible && frustum.intersects(bounds.minimum, bounds.maximum);
            }
            if (!include) {
                const glm::vec3 minimum(bounds.minimum.x - 2, bounds.minimum.y - 2, bounds.minimum.z - 2);
                const glm::vec3 maximum(bounds.maximum.x + 2, bounds.maximum.y + 2, bounds.maximum.z + 2);
                for (const auto& shadow : shadows)
                    if (shadow.intersects(minimum, maximum)) { include = true; break; }
            }
        }
        if (include) selected.push_back(id);
        return selected.size() < maxVisibleZombies;
    });
}

} // namespace multiplayer
