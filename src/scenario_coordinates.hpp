#pragma once

#include <memory>

#include <glm/glm.hpp>

namespace viewer {

struct Model;
struct ZombieScenarioConfig;

// Converts authored east/north rectangles to runtime X east / Z south.
// Geographic scenes use their recorded map origin; unreferenced fixtures use
// the center of their horizontal scene bounds for meter offsets.
class ScenarioCoordinates {
public:
    ScenarioCoordinates(const ZombieScenarioConfig& config, const Model& scene);
    ~ScenarioCoordinates();

    // Sample in the authored rectangle before any geographic projection.
    // Both inputs must be finite and between 0 and 1, inclusive.
    glm::vec2 sample(double unitEast, double unitNorth) const;
    // Check the final float placement against the authored rectangle.
    bool contains(glm::vec2 runtimeXZ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
