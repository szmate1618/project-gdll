#include "graphics_settings_file.hpp"
#include "application_paths.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(const viewer::GraphicsSettings& a, const viewer::GraphicsSettings& b) {
    return a.fullscreen == b.fullscreen && a.resolution == b.resolution && a.distantScenery == b.distantScenery &&
        a.shadowCasters == b.shadowCasters && a.skipHighestDetail == b.skipHighestDetail;
}
struct Scratch {
    std::filesystem::path path = viewer::executableDirectory() /
        ("graphics-file-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { std::filesystem::create_directory(path); }
    ~Scratch() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}
int main() {
    try {
        Scratch scratch;
        const auto defaultPath = viewer::executableDirectory() / "userdata/graphics.toml";
        require(viewer::graphicsSettingsPath() == defaultPath, "Preferences must live beside the executable");
        const auto previous = std::filesystem::current_path();
        std::filesystem::current_path(scratch.path);
        const auto fromOtherDirectory = viewer::graphicsSettingsPath();
        std::filesystem::current_path(previous);
        require(fromOtherDirectory == defaultPath, "Working directory must not change the preference location");
        const auto path = scratch.path / std::filesystem::u8path(u8"user data/Áron/graphics.toml");
        require(!viewer::loadGraphicsSettings(path), "Missing settings must use defaults");
        require(!std::filesystem::exists(path.parent_path()), "Reading missing settings must not create directories");
        viewer::GraphicsSettings chosen;
        chosen.fullscreen = true; chosen.resolution = 2; chosen.distantScenery = 0;
        chosen.shadowCasters = viewer::ShadowCasters::Npcs; chosen.skipHighestDetail = true;
        viewer::saveGraphicsSettings(path, chosen);
        require(same(*viewer::loadGraphicsSettings(path), chosen), "All graphics settings must survive a save/load round trip");
        for (int resolution = 0; resolution < 3; ++resolution)
            for (int distance = 0; distance < 4; ++distance)
                for (int casters = 0; casters < 5; ++casters) {
                    chosen.resolution = resolution; chosen.distantScenery = distance;
                    chosen.shadowCasters = static_cast<viewer::ShadowCasters>(casters);
                    viewer::saveGraphicsSettings(path, chosen);
                    require(same(*viewer::loadGraphicsSettings(path), chosen), "Every supported selector value must round-trip");
                }
        viewer::saveGraphicsSettings(path, viewer::GraphicsSettings{});
        require(same(*viewer::loadGraphicsSettings(path), viewer::GraphicsSettings{}), "Reset defaults must replace an existing save");
        auto invalid = chosen; invalid.resolution = 5;
        bool rejected = false;
        try { viewer::saveGraphicsSettings(path, invalid); } catch (const std::exception&) { rejected = true; }
        require(rejected && same(*viewer::loadGraphicsSettings(path), viewer::GraphicsSettings{}),
                "Invalid writes must preserve the last good preferences");
        const std::string valid = "schema_version = 1\nfullscreen = false\nrender_resolution = \"1280x720\"\n"
            "distant_scenery_meters = 0\nshadow_casters = \"objects\"\nskip_highest_detail = false\n";
        const auto malformed = scratch.path / "invalid.toml";
        for (const auto& replacement : {std::pair<std::string, std::string>{"schema_version = 1", "schema_version = 9"},
                {"fullscreen = false", "fullscreen = 1"}, {"1280x720", "800x600"},
                {"distant_scenery_meters = 0", "distant_scenery_meters = 501"}, {"objects", "anything"},
                {"skip_highest_detail = false", "skip_highest_detail = 'false'"}}) {
            auto contents = valid;
            contents.replace(contents.find(replacement.first), replacement.first.size(), replacement.second);
            std::ofstream(malformed, std::ios::binary) << contents;
            rejected = false;
            try { (void)viewer::loadGraphicsSettings(malformed); } catch (const std::exception&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Invalid setting was accepted: " + replacement.second);
        }
        require(!viewer::readGraphicsSettings(malformed), "Startup must recover from a bad save");
        require(std::filesystem::exists(malformed), "Reading a bad save must leave it available for recovery");
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
            require(entry.path() == path, "Successful saves must not leave temporary files behind");
        std::cout << "Graphics preference persistence tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
