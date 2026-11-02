#pragma once
#include "graphics_settings.hpp"
#include <filesystem>
#include <optional>
namespace viewer {
std::filesystem::path graphicsSettingsPath();
void validateGraphicsSettings(const GraphicsSettings& settings);
// Missing files return null; malformed/unsupported settings throw without writes.
std::optional<GraphicsSettings> loadGraphicsSettings(const std::filesystem::path& path);
// Startup fallback: report invalid/unreadable saves and leave them untouched.
std::optional<GraphicsSettings> readGraphicsSettings(const std::filesystem::path& path);
// Validate and replace atomically; failures preserve the last good destination.
void saveGraphicsSettings(const std::filesystem::path& path, const GraphicsSettings& settings);
}
