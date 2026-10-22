#include "scenario_coordinates.hpp"

#include "model.hpp"
#include "zombie_scenario_config.hpp"

#include <proj.h>

#include <array>
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

struct ScenarioCoordinates::Impl {
    ZombieHordeConfig::Rectangle bounds;
    glm::dvec2 projectedOrigin{0}; // Projected east/north meters.
    PJ_CONTEXT* context = nullptr;
    PJ* projection = nullptr;

    ~Impl() {
        proj_destroy(projection);
        proj_context_destroy(context);
    }

    glm::dvec2 transform(glm::dvec2 point, PJ_DIRECTION direction) const {
        proj_errno_reset(projection);
        const auto result = proj_trans(projection, direction, proj_coord(point.x, point.y, 0, 0));
        const glm::dvec2 output(result.xy.x, result.xy.y);
        if (proj_errno(projection) || !finite(output))
            invalid("cannot transform WGS84 coordinates using the map CRS");
        return output;
    }
};

ScenarioCoordinates::ScenarioCoordinates(ZombieHordeConfig::CoordinateSystem coordinates,
    const ZombieHordeConfig::Rectangle& bounds, const Model& scene)
    : impl_(std::make_unique<Impl>()) {
    impl_->bounds = bounds;
    for (const auto coordinate : bounds)
        if (!std::isfinite(coordinate)) invalid("rectangle coordinates must be finite");
    if (bounds[0] >= bounds[2] || bounds[1] >= bounds[3])
        invalid("rectangle requires minimum bounds smaller than maximum bounds");

    if (coordinates == ZombieHordeConfig::CoordinateSystem::World) return;
    if (bounds[0] < -180 || bounds[2] > 180 || bounds[1] < -90 || bounds[3] > 90)
        invalid("WGS84 rectangle must contain longitude/latitude degrees");
    if (!scene.geographicFrame || scene.geographicFrame->crs.empty() ||
        !finite(scene.geographicFrame->origin))
        invalid("WGS84 rectangles require geographic metadata in the map");

    impl_->projectedOrigin = scene.geographicFrame->origin;
    impl_->context = proj_context_create();
    if (!impl_->context) invalid("cannot create the geographic projection context");
    proj_context_set_enable_network(impl_->context, 0);
    PJ* raw = proj_create_crs_to_crs(impl_->context, "EPSG:4326",
                                   scene.geographicFrame->crs.c_str(), nullptr);
    if (!raw) invalid("cannot project WGS84 coordinates to map CRS '" + scene.geographicFrame->crs + "'");
    // EPSG:4326 formally orders latitude first. Authored rectangles and the
    // generator both use longitude first; normalized PROJ axes preserve that.
    impl_->projection = proj_normalize_for_visualization(impl_->context, raw);
    proj_destroy(raw);
    if (!impl_->projection) invalid("cannot normalize geographic coordinate axes");
    for (const auto& unit : std::array<glm::dvec2, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}})
        (void)sample(unit.x, unit.y);
}

ScenarioCoordinates::~ScenarioCoordinates() = default;

glm::vec2 ScenarioCoordinates::sample(double unitX, double unitY) const {
    if (!std::isfinite(unitX) || !std::isfinite(unitY) ||
        unitX < 0 || unitX > 1 || unitY < 0 || unitY > 1)
        invalid("sample inputs must be between zero and one");
    const auto& bounds = impl_->bounds;
    const glm::dvec2 point(bounds[0] + (bounds[2] - bounds[0]) * unitX,
                           bounds[1] + (bounds[3] - bounds[1]) * unitY);
    glm::dvec2 runtime;
    if (impl_->projection) {
        const auto projected = impl_->transform(point, PJ_FWD) - impl_->projectedOrigin;
        runtime = {projected.x, -projected.y};
    } else {
        runtime = point;
    }
    if (!finite(runtime) || std::abs(runtime.x) > std::numeric_limits<float>::max() ||
        std::abs(runtime.y) > std::numeric_limits<float>::max())
        invalid("sample is outside the runtime coordinate range");
    return glm::vec2(runtime);
}

bool ScenarioCoordinates::contains(glm::vec2 runtimeXZ) const {
    if (!finite(glm::dvec2(runtimeXZ))) return false;
    glm::dvec2 point;
    double tolerance;
    if (impl_->projection) {
        point = impl_->transform(impl_->projectedOrigin + glm::dvec2(runtimeXZ.x, -runtimeXZ.y), PJ_INV);
        // Accommodate float runtime placement rounding, about a millimeter.
        tolerance = 1e-8;
    } else {
        point = glm::dvec2(runtimeXZ);
        tolerance = 0.001;
    }
    const auto& bounds = impl_->bounds;
    return point.x >= bounds[0] - tolerance && point.x <= bounds[2] + tolerance &&
           point.y >= bounds[1] - tolerance && point.y <= bounds[3] + tolerance;
}

} // namespace viewer
