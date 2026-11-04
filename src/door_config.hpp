#pragma once

#include <filesystem>

namespace viewer {
struct DoorConfig {
    // Elastic free-edge travel along the closed panel's normal, in meters.
    // With breaking enabled, overload and fresh stretching gradually add permanent slack.
    float maxClosedFlexMeters = .02f;
    float breakFlexMeters = .05f; // Permanent allowance at release; zero keeps a fixed stop.
};
void validateDoorConfig(const DoorConfig& config);
DoorConfig loadDoorConfig(const std::filesystem::path& path);
} // namespace viewer
