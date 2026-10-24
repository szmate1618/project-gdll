#include "scenario_coordinates.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace viewer {
namespace {

bool finite(glm::dvec2 point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("scenario coordinates: " + message);
}

} // namespace

ScenarioCoordinates::ScenarioCoordinates(const ZombieHordeConfig::Rectangle& rectangle)
    : bounds_(rectangle) {
    for (const auto coordinate : bounds_)
        if (!std::isfinite(coordinate)) invalid("rectangle coordinates must be finite");
    if (bounds_[0] >= bounds_[2] || bounds_[1] >= bounds_[3])
        invalid("rectangle requires minimum bounds smaller than maximum bounds");
}

glm::vec2 ScenarioCoordinates::sample(double unitX, double unitY) const {
    if (!std::isfinite(unitX) || !std::isfinite(unitY) ||
        unitX < 0 || unitX > 1 || unitY < 0 || unitY > 1)
        invalid("sample inputs must be between zero and one");
    const glm::dvec2 runtime(bounds_[0] + (bounds_[2] - bounds_[0]) * unitX,
                             bounds_[1] + (bounds_[3] - bounds_[1]) * unitY);
    if (!finite(runtime) || std::abs(runtime.x) > std::numeric_limits<float>::max() ||
        std::abs(runtime.y) > std::numeric_limits<float>::max())
        invalid("sample is outside the runtime coordinate range");
    return glm::vec2(runtime);
}

bool ScenarioCoordinates::contains(glm::vec2 runtimeXZ) const {
    if (!finite(glm::dvec2(runtimeXZ))) return false;
    const glm::dvec2 point(runtimeXZ);
    constexpr double tolerance = 0.001; // One millimeter for float placement rounding.
    return point.x >= bounds_[0] - tolerance && point.x <= bounds_[2] + tolerance &&
           point.y >= bounds_[1] - tolerance && point.y <= bounds_[3] + tolerance;
}

} // namespace viewer
