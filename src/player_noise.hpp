#pragma once

#include "noise.hpp"

#include <vector>

namespace viewer {

struct PlayerNoiseMotion {
    glm::vec3 feet{0};
    bool grounded = false;
};

// Silent simulation events, driven by actual movement in player physics steps.
// Walking emits 1.5 footsteps/s, running 3/s, and landing emits one louder noise.
class PlayerNoiseEmitter {
public:
    std::vector<NoiseEvent> update(float seconds, const PlayerNoiseMotion& before,
                                   const PlayerNoiseMotion& after, bool running);
    void reset() { footstepPhase_ = 0; }

private:
    double footstepPhase_ = 0; // Fraction of a step; retained across speed changes.
};

} // namespace viewer
