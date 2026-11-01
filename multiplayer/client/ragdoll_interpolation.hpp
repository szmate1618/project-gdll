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

// Extrapolate world joint motion from two authoritative timestamps. Prediction
// is bounded to 400 ms and rejects discontinuities; fixed affine bases survive.
CorpsePose predictRagdollPose(const CorpsePose& before, const CorpsePose& after,
                            const viewer::AnimatedModel& asset, double seconds);

// Apply a fading world-joint correction measured from target to displayed.
// The current predicted trajectory keeps advancing while its error decays.
CorpsePose correctRagdollPose(const CorpsePose& displayed, const CorpsePose& target,
                            const CorpsePose& predicted, const viewer::AnimatedModel& asset, float weight);

} // namespace multiplayer
