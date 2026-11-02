#include "graphics_settings_file.hpp"
#include "application_paths.hpp"
#include <toml++/toml.hpp>
#include <chrono>
#include <cstdint>
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
    if (!value) throw std::runtime_error(std::string("Missing or invalid graphics setting: ") + key);
    return *value;
}
std::string resolutionName(int index) {
    const auto size = renderResolutions.at(index);
    return std::to_string(size.x) + "x" + std::to_string(size.y);
}
}
std::filesystem::path graphicsSettingsPath() { return userDataDirectory() / "graphics.toml"; }
void validateGraphicsSettings(const GraphicsSettings& settings) {
    if (settings.resolution < 0 || settings.resolution >= static_cast<int>(renderResolutions.size()) ||
        settings.distantScenery < 0 || settings.distantScenery >= static_cast<int>(distantSceneryDistances.size()) ||
        settings.shadowCasters < ShadowCasters::None || settings.shadowCasters > ShadowCasters::Objects)
        throw std::invalid_argument("Invalid graphics settings");
}
std::optional<GraphicsSettings> loadGraphicsSettings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read graphics settings");
    const auto table = toml::parse(input, path.u8string());
    if (required<std::int64_t>(table, "schema_version") != 1) throw std::runtime_error("Unsupported graphics settings version");
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
std::optional<GraphicsSettings> readGraphicsSettings(const std::filesystem::path& path) {
    try { return loadGraphicsSettings(path); }
    catch (const std::exception& error) {
        std::cerr << "Graphics settings: " << error.what() << "; using startup defaults (" << path.u8string() << ")\n";
        return {};
    }
}
void saveGraphicsSettings(const std::filesystem::path& path, const GraphicsSettings& settings) {
    validateGraphicsSettings(settings);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << "schema_version = 1\nfullscreen = " << (settings.fullscreen ? "true" : "false")
                   << "\nrender_resolution = \"" << resolutionName(settings.resolution)
                   << "\"\ndistant_scenery_meters = " << static_cast<int>(distantSceneryDistances[settings.distantScenery])
                   << "\nshadow_casters = \"" << casterNames[static_cast<int>(settings.shadowCasters)]
                   << "\"\nskip_highest_detail = " << (settings.skipHighestDetail ? "true" : "false") << '\n';
            output.flush();
            if (!output) throw std::runtime_error("Cannot write graphics settings");
        }
        (void)loadGraphicsSettings(temporary);
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace graphics settings");
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
}
