#pragma once
#include "model.hpp"
#include "vehicle_config.hpp"

namespace viewer {
// Apparent distance at 60-degree vertical FOV; thresholds in meters. A 10%
// hysteresis band prevents oscillation. Policy affects rendering alone.
struct VehicleDetail { std::size_t level = 0; bool visible = true; };
VehicleDetail vehicleCameraDetail(float distance, float projectionScale,
                                 std::size_t previous = 0, bool enabled = true);
// Four chassis-local static meshes. Missing/stale assets return an empty vector
// with a diagnostic; the renderer retains the animated prototype fallback.
std::vector<Model> loadVehicleLods(const std::filesystem::path& directory, const std::string& id,
                                 const VehicleConfig* config = nullptr);
}
