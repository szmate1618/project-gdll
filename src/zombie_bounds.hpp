#pragma once

#include "animated_model.hpp"
#include "visibility.hpp"

namespace viewer {

// Baked animation positions and skinned bind vertices use different asset
// frames. Keep their extents separate until a live palette is available.
void includeZombieGeometryBounds(const AnimatedModel& asset, VisibilityBounds& animation,
                                VisibilityBounds& bind);
VisibilityBounds transformedZombieBounds(const VisibilityBounds& bounds, const glm::mat4& transform);
VisibilityBounds emptyZombieBounds();
void includeZombieBounds(VisibilityBounds& target, const VisibilityBounds& other);

} // namespace viewer
