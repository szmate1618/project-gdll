#pragma once

#include "protocol.hpp"

namespace viewer { struct AnimatedModel; }

namespace multiplayer {

// Interpolate complete world-space joint poses, preserving each bone's
// fixed affine basis (including scale, reflection and shear). Incompatible
// mesh variants or non-rigid/singular changes snap to the complete newer pose.
// Endpoints are copied exactly; the returned intermediate pose uses after's
// version and must not be treated as an authoritative received sample.
CorpsePose interpolateRagdollPose(const CorpsePose& before, const CorpsePose& after,
                                const viewer::AnimatedModel& asset, float alpha);

} // namespace multiplayer
