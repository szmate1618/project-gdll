#include "zombie_lod.hpp"
#include <cmath>
namespace viewer {
ZombieDetail zombieCameraDetail(float distance, float projectionScale, bool enabled, float maximumDistance) {
    if (!enabled) return {};
    // Normalize apparent size to a 60-degree vertical FOV. Zoom keeps detail
    // and extends the cutoff so a scope does not hide its distant targets.
    const float effective = distance * std::sqrt(3.0f) / projectionScale;
    ZombieDetail result;
    result.visible = maximumDistance <= 0 || effective <= maximumDistance;
    if (effective >= 80) result = {3, 5, result.visible};
    else if (effective >= 35) result = {2, 10, result.visible};
    else if (effective >= 12) result = {1, 15, result.visible};
    return result;
}
}
