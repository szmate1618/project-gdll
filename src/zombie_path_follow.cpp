#include "zombie_path_follow.hpp"
#include "zombie_navmesh.hpp"

#include <algorithm>
#include <cmath>

namespace viewer::steering {
namespace {
constexpr double arrivalRadius = .15;
constexpr double reachedRadius = 1e-5;

bool finite(navmesh_path::Point point) {
    return std::isfinite(point.x) && std::isfinite(point.z);
}

double distance(navmesh_path::Point a, navmesh_path::Point b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
}

glm::vec2 followPath(const navmesh_path::Point* points, std::size_t count,
                     glm::vec2 position, float speed, float dt, FollowState& state,
                     const navmesh_path::Mesh* mesh) {
    const navmesh_path::Point current{position.x, position.y};
    state.arrived = false;
    if (!points || count == 0) {
        state.segment = 0;
        return glm::vec2(0);
    }
    if (!finite(current) || !finite(points[count - 1]) || !std::isfinite(speed) ||
        !std::isfinite(dt) || speed <= 0 || dt <= 0) return glm::vec2(0);
    if (distance(current, points[count - 1]) <= arrivalRadius) {
        state.segment = count - 1;
        state.arrived = true;
        return glm::vec2(0);
    }

    state.segment = std::min(state.segment, count > 1 ? count - 2 : std::size_t(0));
    std::size_t target = count > 1 ? state.segment + 1 : 0;
    while (target + 1 < count) {
        const auto a = points[state.segment], b = points[target];
        if (!finite(a) || !finite(b) || !finite(points[target + 1])) return glm::vec2(0);
        const double gap = distance(current, b);
        const double dx = b.x - a.x, dz = b.z - a.z;
        const bool passed = dx * (current.x - b.x) + dz * (current.z - b.z) >= 0;
        const bool reached = gap <= reachedRadius;
        // A skipped corner changes the path. Validate that complete shortcut,
        // including corners passed while local avoidance displaced the agent.
        const bool eligible = reached || (mesh && (gap <= arrivalRadius || passed));
        if (!eligible || (mesh && !mesh->segmentClear(current, points[target + 1]))) break;
        ++state.segment;
        ++target;
    }
    if (!finite(points[target])) return glm::vec2(0);
    const double gap = distance(current, points[target]);
    if (!std::isfinite(gap) || gap <= reachedRadius) return glm::vec2(0);
    const double magnitude = std::min(double(speed), gap / double(dt));
    return glm::vec2(float((points[target].x - current.x) / gap * magnitude),
                     float((points[target].z - current.z) / gap * magnitude));
}

} // namespace viewer::steering
