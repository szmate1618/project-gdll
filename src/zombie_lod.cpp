#include "zombie_lod.hpp"
#include <cmath>
#include <limits>
namespace viewer {
std::optional<float> zombieCameraDrawRadius(float projectionScale, bool enabled, float maximumDistance) {
    if (!enabled || maximumDistance <= 0 || !std::isfinite(maximumDistance) ||
        !std::isfinite(projectionScale) || projectionScale <= 0) return std::nullopt;
    // Invert the effective-distance policy with outward rounding. False
    // positives are harmless; rejecting a boundary actor would change rendering.
    const float radius = maximumDistance * projectionScale / std::sqrt(3.0f);
    const float padded = radius * (1 + 8 * std::numeric_limits<float>::epsilon());
    if (!std::isfinite(padded)) return std::nullopt;
    return std::nextafter(padded, std::numeric_limits<float>::infinity());
}
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
