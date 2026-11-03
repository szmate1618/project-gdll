#pragma once

#include <filesystem>

namespace viewer {
struct DoorConfig {
    // Elastic free-edge travel along the closed panel's normal, in meters.
    // With breaking enabled, stretching farther permanently lengthens the restraint.
    float maxClosedFlexMeters = .01f;
    float breakFlexMeters = .05f; // Actual stretch to release; zero keeps a fixed stop.
};
void validateDoorConfig(const DoorConfig& config);
DoorConfig loadDoorConfig(const std::filesystem::path& path);
} // namespace viewer
