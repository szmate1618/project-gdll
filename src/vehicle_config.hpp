#pragma once
#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <array>

namespace viewer {
constexpr std::size_t maxVehicleSeats = 4, maxVehicleTypes = 32, maxVehicleInstances = 1024;
// All vehicle types share this placement pivot, including tilted GIS placements.
constexpr float vehiclePivotHeight = .68f;
// Chassis-local meters: X right, Y up, -Z forward. Index 0 is front-left.
struct VehicleSeatConfig {
    glm::vec3 eyePosition, entryPosition, exitPosition;
    float entryRadius = 2;
};
struct VehicleConfig {
    std::string id = "prototype", name = "Prototype car";
    float weightKg = 1100; // Total mass, including the four 25 kg wheels.
    // Full fitted mesh bounds (including mirrors), in meters. Length is the
    // real-car reference; width/height retain the authored mesh proportions.
    glm::vec3 dimensions{1.7f, 1.56f, 3.6f}; // width, height, length
    float tireRadius = .36f, tireWidth = .24f;
    std::array<glm::vec3, 4> wheels{{{-.83f,-.32f,-1.15f},{.83f,-.32f,-1.15f},
                                  {-.83f,-.32f,1.15f},{.83f,-.32f,1.15f}}};
    std::array<glm::vec3, 2> headlights{{{-.64f,.08f,-1.83f},{.64f,.08f,-1.83f}}};
    std::vector<VehicleSeatConfig> seats{
        {{-.38f,.9f,-.25f},{-1.1f,.8f,-.25f},{-1.65f,.4f,-.25f},2},
        {{ .38f,.9f,-.25f},{ 1.1f,.8f,-.25f},{ 1.65f,.4f,-.25f},2},
        {{-.38f,.9f,1.15f},{-1.1f,.8f,1.15f},{-1.65f,.4f,1.15f},2},
        {{ .38f,.9f,1.15f},{ 1.1f,.8f,1.15f},{ 1.65f,.4f,1.15f},2}};
};
void validateVehicleConfig(const VehicleConfig& config);
VehicleConfig loadVehicleConfig(const std::filesystem::path& path);
std::vector<std::filesystem::path> vehicleConfigFiles(const std::filesystem::path& directory);
std::vector<VehicleConfig> loadVehicleConfigs(const std::filesystem::path& directory);
const char* vehicleSeatName(std::size_t seat);
} // namespace viewer
