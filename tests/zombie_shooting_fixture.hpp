#pragma once

#include "zombie_layer.hpp"

namespace viewer::test {

// Ragdoll/collision fixtures need a lethal sequence at one unchanged pose.
inline bool shootToKill(ZombieLayer& layer, glm::vec3 origin, glm::vec3 direction, double seconds = 0) {
    const auto corpses = layer.ragdollCount();
    for (int shot = 0; shot < 10; ++shot) {
        if (!layer.shoot(origin, direction, seconds)) return false;
        if (layer.ragdollCount() > corpses) return true;
    }
    return false;
}

} // namespace viewer::test
