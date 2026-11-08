#pragma once
#include "settings.hpp"
#include <filesystem>
#include <optional>
namespace viewer {
std::filesystem::path settingsPath();
void validateSettings(const Settings& settings);
void validateGraphicsSettings(const GraphicsSettings& settings);
// Missing files return null; malformed/unsupported settings throw without writes.
std::optional<Settings> loadSettings(const std::filesystem::path& path);
// If the shared file is missing, import graphics.toml without modifying it.
// Startup fallback: report invalid/unreadable saves and leave them untouched.
std::optional<Settings> readSettings(const std::filesystem::path& path);
// Validate and replace atomically; failures preserve the last good destination.
void saveSettings(const std::filesystem::path& path, const Settings& settings);
// Runtime fallback: report save failures without interrupting gameplay. Empty paths disable saving.
void writeSettings(const std::filesystem::path& path, const Settings& settings);
}
