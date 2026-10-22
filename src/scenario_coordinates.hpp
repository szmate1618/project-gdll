#pragma once

#include "zombie_scenario_config.hpp"

#include <memory>

#include <glm/glm.hpp>

namespace viewer {

struct Model;
// World rectangles are absolute runtime X east / Z south meters.
// WGS84 rectangles are projected using the scene's recorded CRS and origin.
class ScenarioCoordinates {
public:
    ScenarioCoordinates(ZombieHordeConfig::CoordinateSystem coordinates,
                        const ZombieHordeConfig::Rectangle& rectangle, const Model& scene);
    ~ScenarioCoordinates();

    // Sample in the authored rectangle before any geographic projection.
    // Both inputs must be finite and between 0 and 1, inclusive.
    glm::vec2 sample(double unitX, double unitY) const;
    // Check the final float placement against the authored rectangle.
    bool contains(glm::vec2 runtimeXZ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
