#pragma once

#include "scenario_coordinates.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace viewer {

// Piecewise constant relative weights from an 8/16-bit grayscale PNG, stretched
// over world X/Z bounds. Image top-left is (min X, min Z); no gamma correction.
class ScenarioDensity {
public:
    ScenarioDensity(const std::filesystem::path& path, const ZombieHordeConfig::Rectangle& bounds);

    // Independent random inputs in [0,1): pixel selection and position inside it.
    glm::vec2 sample(double selection, double pixelX, double pixelZ) const;
    // Used after nearby adjustment to preserve the image's zero-density mask.
    bool allows(glm::vec2 position) const;

private:
    ZombieHordeConfig::Rectangle bounds_;
    ScenarioCoordinates coordinates_;
    int width_ = 0, height_ = 0;
    std::vector<std::uint16_t> weights_;
    std::vector<double> cumulative_;
};

} // namespace viewer
