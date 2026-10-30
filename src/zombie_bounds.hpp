#pragma once

#include "animated_model.hpp"
#include "visibility.hpp"

namespace viewer {

// Baked animation positions and skinned bind vertices use different asset
// frames. Keep their extents separate until a live palette is available.
void includeZombieGeometryBounds(const AnimatedModel& asset, VisibilityBounds& animation,
                                VisibilityBounds& bind);
// Cache the animation extent, then free CPU vertex frames and their capacity.
// Call only after all GPU geometry (including wound closures) has been uploaded.
// Retains bone frames, bind vertices and weights for gameplay. Idempotent;
// another renderer upload requires reloading the asset.
void releaseZombieAnimationVertices(AnimatedModel& asset);
VisibilityBounds transformedZombieBounds(const VisibilityBounds& bounds, const glm::mat4& transform);
VisibilityBounds emptyZombieBounds();
void includeZombieBounds(VisibilityBounds& target, const VisibilityBounds& other);

} // namespace viewer
