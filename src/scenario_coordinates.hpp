#pragma once

#include "zombie_scenario_config.hpp"

#include <glm/glm.hpp>

namespace viewer {

// Rectangles are absolute runtime X east / Z south meters, independent of map metadata.
class ScenarioCoordinates {
public:
    explicit ScenarioCoordinates(const ZombieHordeConfig::Rectangle& rectangle);

    // Sample in the authored world rectangle.
    // Both inputs must be finite and between 0 and 1, inclusive.
    glm::vec2 sample(double unitX, double unitY) const;
    // Check the final float placement against the authored rectangle.
    bool contains(glm::vec2 runtimeXZ) const;

private:
    ZombieHordeConfig::Rectangle bounds_;
};

} // namespace viewer
