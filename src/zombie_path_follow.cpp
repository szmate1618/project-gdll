#include "zombie_update_profile.hpp"
#include "zombie_path_follow.hpp"
#include "zombie_navmesh.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace viewer::steering {
ApproximatePathStep advancePathApproximate(const navmesh_path::Point* points, std::size_t count,
    glm::vec2 position, float distanceBudget, FollowState& state, const std::vector<glm::vec3>& sleepers, float wakeRadius) {
    ApproximatePathStep result{position};
    if (!points || !count || !std::isfinite(distanceBudget) || distanceBudget < 0) return result;
    auto progress = state;
    progress.segment = std::min(progress.segment, count - 1);
    auto cursor = glm::dvec2(position);
    double left = distanceBudget;
    while (progress.segment + 1 < count && left > 0) {
        const glm::dvec2 target(points[progress.segment + 1].x, points[progress.segment + 1].z);
        const auto delta = target - cursor;
        const double gap = glm::length(delta), length = std::min(left, gap);
        if (gap < 1e-8) { ++progress.segment; continue; }
        const auto end = cursor + delta * (length / gap), step = end - cursor;
        for (const auto feet : sleepers) {
            const glm::dvec2 sleeper(feet.x, feet.z);
            const double t = glm::dot(step, step) > 0 ? std::clamp(glm::dot(sleeper-cursor, step)/glm::dot(step,step), 0., 1.) : 0;
            if (glm::length(sleeper - (cursor + step*t)) <= wakeRadius) {
                result.nearSleeper = true;
                return result;
            }
        }
        cursor = end; left -= length; result.travel += static_cast<float>(length);
        if (length >= gap) ++progress.segment;
    }
    result.position = glm::vec2(cursor);
    result.arrived = progress.arrived = progress.segment + 1 >= count;
    state = progress;
    return result;
}
namespace {
constexpr double arrivalRadius = .15;
constexpr double reachedRadius = 1e-5;

bool finite(navmesh_path::Point point) {
    return std::isfinite(point.x) && std::isfinite(point.z);
}

double distance(navmesh_path::Point a, navmesh_path::Point b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

bool withinRadius(navmesh_path::Point a,navmesh_path::Point b,double radius) {
    const double dx=a.x-b.x,dz=a.z-b.z;
    // Far waypoints cannot satisfy the radius. Keep hypot for the boundary
    // neighborhood rather than changing rounding with squared comparisons.
    return std::abs(dx)<=radius*2 && std::abs(dz)<=radius*2 && std::hypot(dx,dz)<=radius;
}
}

glm::vec2 followPath(const navmesh_path::Point* points, std::size_t count,
                     glm::vec2 position, float speed, float dt, FollowState& state,
                     const navmesh_path::Mesh* mesh) {
    zombie_profile::Timer timer(zombie_profile::Stage::pathFollowing);
    const navmesh_path::Point current{position.x, position.y};
    state.arrived = false;
    if (!points || count == 0) {
        state.segment = 0;
        return glm::vec2(0);
    }
    if (!finite(current) || !finite(points[count - 1]) || !std::isfinite(speed) ||
        !std::isfinite(dt) || speed <= 0 || dt <= 0) return glm::vec2(0);
    if (withinRadius(current,points[count-1],arrivalRadius)) {
        state.segment = count - 1;
        state.arrived = true;
        return glm::vec2(0);
    }

    state.segment = std::min(state.segment, count > 1 ? count - 2 : std::size_t(0));
    std::size_t target = count > 1 ? state.segment + 1 : 0;
    while (target + 1 < count) {
        const auto a = points[state.segment], b = points[target];
        if (!finite(a) || !finite(b) || !finite(points[target + 1])) return glm::vec2(0);
        const double dx = b.x - a.x, dz = b.z - a.z;
        const bool passed = dx * (current.x - b.x) + dz * (current.z - b.z) >= 0;
        const bool reached = withinRadius(current,b,reachedRadius);
        // A skipped corner changes the path. Validate that complete shortcut,
        // including corners passed while local avoidance displaced the agent.
        const bool eligible = reached || (mesh && (passed || withinRadius(current,b,arrivalRadius)));
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
