#include "settings_file.hpp"
#include "application_paths.hpp"
#include <chrono>
#include <limits>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(const viewer::Settings& first, const viewer::Settings& second) {
    const auto& a = first.graphics; const auto& b = second.graphics;
    return a.fullscreen == b.fullscreen && a.resolution == b.resolution && a.distantScenery == b.distantScenery &&
        a.shadowCasters == b.shadowCasters && a.skipHighestDetail == b.skipHighestDetail && first.audio.volume == second.audio.volume;
}
struct Scratch {
    std::filesystem::path path = viewer::executableDirectory() /
        ("settings-file-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { std::filesystem::create_directory(path); }
    ~Scratch() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}
int main() {
    try {
        Scratch scratch;
        const auto defaultPath = viewer::executableDirectory() / "userdata/settings.toml";
        require(viewer::settingsPath() == defaultPath, "Preferences must live beside the executable");
        const auto previous = std::filesystem::current_path();
        std::filesystem::current_path(scratch.path);
        const auto fromOtherDirectory = viewer::settingsPath();
        std::filesystem::current_path(previous);
        require(fromOtherDirectory == defaultPath, "Working directory must not change the preference location");
        const auto path = scratch.path / std::filesystem::u8path(u8"user data/Áron/settings.toml");
        require(!viewer::loadSettings(path), "Missing settings must use defaults");
        require(!std::filesystem::exists(path.parent_path()), "Reading missing settings must not create directories");
        viewer::Settings chosen;
        chosen.graphics.fullscreen = true; chosen.graphics.resolution = 2; chosen.graphics.distantScenery = 0;
        chosen.graphics.shadowCasters = viewer::ShadowCasters::Npcs; chosen.graphics.skipHighestDetail = true;
        chosen.audio.volume = .37f;
        viewer::saveSettings(path, chosen);
        require(same(*viewer::loadSettings(path), chosen), "All graphics and audio settings must survive a save/load round trip");
        for (int resolution = 0; resolution < 3; ++resolution)
            for (int distance = 0; distance < 4; ++distance)
                for (int casters = 0; casters < 5; ++casters) {
                    chosen.graphics.resolution = resolution; chosen.graphics.distantScenery = distance;
                    chosen.graphics.shadowCasters = static_cast<viewer::ShadowCasters>(casters);
                    viewer::saveSettings(path, chosen);
                    require(same(*viewer::loadSettings(path), chosen), "Every supported selector value must round-trip");
                }
        viewer::saveSettings(path, viewer::Settings{});
        require(same(*viewer::loadSettings(path), viewer::Settings{}), "Reset defaults must replace an existing save");
        auto invalid = chosen; invalid.graphics.resolution = 5;
        bool rejected = false;
        try { viewer::saveSettings(path, invalid); } catch (const std::exception&) { rejected = true; }
        require(rejected && same(*viewer::loadSettings(path), viewer::Settings{}),
                "Invalid writes must preserve the last good preferences");
        for (const float volume : {0.f, .6f, 1.f}) {
            chosen.audio.volume = volume;
            viewer::saveSettings(path, chosen);
            require(same(*viewer::loadSettings(path), chosen), "Mute and full volume must round-trip with graphics intact");
        }
        for (const float volume : {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity()}) {
            auto badAudio = chosen; badAudio.audio.volume = volume;
            rejected = false;
            try { viewer::saveSettings(path, badAudio); } catch (const std::exception&) { rejected = true; }
            require(rejected && same(*viewer::loadSettings(path), chosen), "Invalid volume must preserve the last good save");
        }
        const std::string valid = "schema_version = 1\n[graphics]\nfullscreen = false\nrender_resolution = \"1280x720\"\n"
            "distant_scenery_meters = 0\nshadow_casters = \"objects\"\nskip_highest_detail = false\n[audio]\nvolume = 0.6\n";
        const auto malformed = scratch.path / "invalid.toml";
        for (const auto& replacement : {std::pair<std::string, std::string>{"schema_version = 1", "schema_version = 9"},
                {"fullscreen = false", "fullscreen = 1"}, {"1280x720", "800x600"},
                {"distant_scenery_meters = 0", "distant_scenery_meters = 501"}, {"objects", "anything"},
                {"skip_highest_detail = false", "skip_highest_detail = 'false'"},
                {"volume = 0.6", "volume = -0.1"}, {"volume = 0.6", "volume = 1.1"},
                {"volume = 0.6", "volume = nan"}, {"volume = 0.6", "volume = inf"},
                {"volume = 0.6", "volume = 'loud'"}, {"[audio]", "[missing]"}}) {
            auto contents = valid;
            contents.replace(contents.find(replacement.first), replacement.first.size(), replacement.second);
            std::ofstream(malformed, std::ios::binary) << contents;
            rejected = false;
            try { (void)viewer::loadSettings(malformed); } catch (const std::exception&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Invalid setting was accepted: " + replacement.second);
        }
        require(!viewer::readSettings(malformed), "Startup must recover from a bad save");
        require(std::filesystem::exists(malformed), "Reading a bad save must leave it available for recovery");
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
            require(entry.path() == path, "Successful saves must not leave temporary files behind");
        const auto legacy = scratch.path / "graphics.toml";
        const auto shared = scratch.path / "settings.toml";
        auto legacyText = valid;
        legacyText.erase(legacyText.find("[audio]"));
        legacyText.erase(legacyText.find("[graphics]"), std::string("[graphics]\n").size());
        std::ofstream(legacy, std::ios::binary) << legacyText;
        auto migrated = viewer::readSettings(shared);
        require(migrated && migrated->graphics.distantScenery == 3 && migrated->audio.volume == .6f,
                "Legacy graphics must load with the existing default audio volume");
        require(!std::filesystem::exists(shared), "Reading legacy preferences must not write a shared save");
        migrated->audio.volume = .21f;
        viewer::saveSettings(shared, *migrated);
        require(viewer::readSettings(shared)->audio.volume == .21f, "Shared preferences must take precedence over legacy graphics");
        std::ifstream legacyInput(legacy, std::ios::binary);
        const std::string remainingLegacy((std::istreambuf_iterator<char>(legacyInput)), {});
        require(remainingLegacy == legacyText, "Migration must leave the legacy save untouched");
        std::ofstream(shared, std::ios::binary) << "broken = [";
        require(!viewer::readSettings(shared), "A malformed shared save must not silently load stale legacy settings");
        std::cout << "Shared settings persistence tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
