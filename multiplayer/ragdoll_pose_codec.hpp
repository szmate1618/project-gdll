#pragma once

#include "protocol.hpp"

namespace viewer { struct AnimatedModel; }

namespace multiplayer {
// Quantize parent-relative motion around the authored bone basis. Fixed scale,
// reflection and shear are retained; unsupported changing bases use raw poses.
std::optional<CompactRagdollPose> compactRagdollPose(const viewer::AnimatedModel& asset,
                                                  const CorpsePose& pose);
// Reconstruct a complete palette before ordinary playback/prediction. Throws
// for mismatched rigs or malformed offsets. Clears the wire-only compact data.
void expandRagdollPose(const viewer::AnimatedModel& asset, CorpsePose& pose);
} // namespace multiplayer
