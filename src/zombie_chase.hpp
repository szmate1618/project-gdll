#pragma once

#include "animated_model.hpp"

namespace viewer {

// Add a simple one-second leg cycle to the idle pose. Bones rotate around
// asset-space X (Y up, +Z forward); mapped descendants inherit each swing.
std::vector<glm::mat4> sampleZombieWalkPose(const AnimatedModel& asset,
                                         double animationSeconds, double walkSeconds,
                                         float phase);

} // namespace viewer
