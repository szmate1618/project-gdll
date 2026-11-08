#include "settings_file.hpp"
#include "application_paths.hpp"
#include <toml++/toml.hpp>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace viewer {
namespace {
constexpr std::array<const char*, 5> casterNames{"none", "buildings", "trees", "npcs", "objects"};
template<class T> T required(const toml::table& table, const char* key) {
    const auto value = table[key].value_exact<T>();
    if (!value) throw std::runtime_error(std::string("Missing or invalid setting: ") + key);
    return *value;
}
std::string resolutionName(int index) {
    const auto size = renderResolutions.at(index);
    return std::to_string(size.x) + "x" + std::to_string(size.y);
}
}
std::filesystem::path settingsPath() { return userDataDirectory() / "settings.toml"; }
void validateGraphicsSettings(const GraphicsSettings& settings) {
    if (settings.resolution < 0 || settings.resolution >= static_cast<int>(renderResolutions.size()) ||
        settings.distantScenery < 0 || settings.distantScenery >= static_cast<int>(distantSceneryDistances.size()) ||
        settings.shadowCasters < ShadowCasters::None || settings.shadowCasters > ShadowCasters::Objects)
        throw std::invalid_argument("Invalid graphics settings");
}
namespace {
GraphicsSettings parseGraphics(const toml::table& table) {
    GraphicsSettings result;
    result.fullscreen = required<bool>(table, "fullscreen");
    result.skipHighestDetail = required<bool>(table, "skip_highest_detail");
    const auto resolution = required<std::string>(table, "render_resolution");
    const auto distance = required<std::int64_t>(table, "distant_scenery_meters");
    const auto casters = required<std::string>(table, "shadow_casters");
    result.resolution = -1; result.distantScenery = -1;
    result.shadowCasters = static_cast<ShadowCasters>(-1);
    for (std::size_t i = 0; i < renderResolutions.size(); ++i)
        if (resolution == resolutionName(static_cast<int>(i))) result.resolution = static_cast<int>(i);
    for (std::size_t i = 0; i < distantSceneryDistances.size(); ++i)
        if (distance == distantSceneryDistances[i]) result.distantScenery = static_cast<int>(i);
    for (std::size_t i = 0; i < casterNames.size(); ++i)
        if (casters == casterNames[i]) result.shadowCasters = static_cast<ShadowCasters>(i);
    validateGraphicsSettings(result);
    return result;
}
}
std::optional<Settings> loadSettings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    const auto table = toml::parse_file(path.u8string());
    if (required<std::int64_t>(table, "schema_version") != 1) throw std::runtime_error("Unsupported settings version");
    const auto* graphics = table["graphics"].as_table();
    const auto* audio = table["audio"].as_table();
    if (!graphics || !audio) throw std::runtime_error("Settings require graphics and audio sections");
    Settings result;
    result.graphics = parseGraphics(*graphics);
    const auto volume = (*audio)["volume"].value<double>();
    if (!volume || !std::isfinite(*volume) || *volume < 0 || *volume > 1)
        throw std::runtime_error("Audio volume must be a number between 0 and 1");
    result.audio.volume = static_cast<float>(*volume);
    validateSettings(result);
    return result;
}
std::optional<Settings> readSettings(const std::filesystem::path& path) {
    try {
        if (const auto saved = loadSettings(path)) return saved;
        const auto legacy = path.parent_path() / "graphics.toml";
        if (!std::filesystem::exists(legacy)) return {};
        const auto table = toml::parse_file(legacy.u8string());
        if (required<std::int64_t>(table, "schema_version") != 1)
            throw std::runtime_error("Unsupported legacy graphics settings version");
        return Settings{parseGraphics(table), {}};
    } catch (const std::exception& error) {
        std::cerr << "Settings: " << error.what() << "; using startup defaults (" << path.u8string() << ")\n";
        return {};
    }
}
void validateSettings(const Settings& settings) {
    validateGraphicsSettings(settings.graphics);
    if (!std::isfinite(settings.audio.volume) || settings.audio.volume < 0 || settings.audio.volume > 1)
        throw std::invalid_argument("Audio volume must be between 0 and 1");
}
void saveSettings(const std::filesystem::path& path, const Settings& preferences) {
    validateSettings(preferences);
    const auto& settings = preferences.graphics;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << "schema_version = 1\n\n[graphics]\nfullscreen = " << (settings.fullscreen ? "true" : "false")
                   << "\nrender_resolution = \"" << resolutionName(settings.resolution)
                   << "\"\ndistant_scenery_meters = " << static_cast<int>(distantSceneryDistances[settings.distantScenery])
                   << "\nshadow_casters = \"" << casterNames[static_cast<int>(settings.shadowCasters)]
                   << "\"\nskip_highest_detail = " << (settings.skipHighestDetail ? "true" : "false")
                   << "\n\n[audio]\nvolume = " << toml::value<double>(preferences.audio.volume) << '\n';
            output.flush();
            if (!output) throw std::runtime_error("Cannot write settings");
        }
        (void)loadSettings(temporary);
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace settings");
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
void writeSettings(const std::filesystem::path& path, const Settings& settings) {
    if (path.empty()) return;
    try { saveSettings(path, settings); }
    catch (const std::exception& error) {
        std::cerr << "Settings could not be saved to " << path.u8string() << ": " << error.what() << '\n';
    }
}
}
