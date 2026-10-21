#pragma once
#include "zombie_steering.hpp"

namespace viewer::steering_benchmark {
struct SweepResult {
    glm::vec2 position{};
    bool constrained = false;
    bool invalidStart = false;
};
// Shared experiment-only safety after production body movement. For the flat
// trunk fixtures, clip a straight accepted timestep before its first inflated
// trunk-circle intersection. No radius shrink or penetration tolerance is used.
// The nearby set must include every trunk that could intersect this short step.
SweepResult constrainTrunkSweep(glm::vec2 start,glm::vec2 proposed,
                               const steering::Obstacle* obstacles,std::size_t count,float bodyRadius);
}
