#include "client/host_setup.hpp"
#include "session_settings.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, const char* content) {
    std::ofstream file(path); file << content;
    require(bool(file), "Cannot write host setup fixture");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Fixture directory missing");
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        write(directory / "idle.toml", "version = 5\nseed = 42\n[zombies.residents]\ncount = 250\n");
        write(directory / "travel.toml", "[zombies.horde]\ncount = 600\nmovement = 'travel'\n");
        write(directory / "broken.toml", "[zombies.horde]\ncount = -1\n");
        multiplayer::Options defaults; defaults.map = directory / "map.glb";
        defaults.birds = true; defaults.port = 31234; defaults.dayNight = {7.25, 3600};
        multiplayer::HostSetup setup(defaults, {directory, directory});
        require(setup.scenarios().size() == 4, "Catalog did not deduplicate paths or retain invalid entries");
        auto options = setup.options();
        require(options.scenario.empty() && options.scene.birds && options.scene.port == 31234 &&
                options.scene.dayNight.startHour == 7.25, "Host defaults lost command-line settings");
        for (const auto* port : {"", "0", "65536", "-1", "12x", " 123", "+123"}) {
            setup.port = port; rejects([&] { setup.options(); }, "Invalid port accepted");
        }
        setup.port = "65535";
        for (std::size_t i = 1; i < setup.scenarios().size(); ++i) {
            setup.selected = i;
            if (setup.scenarios()[i].path.stem() == "idle") {
                setup.birds = false; setup.startHour = 0; setup.dayLengthSeconds = 0;
                options = setup.options();
                require(options.scenario.filename() == "idle.toml" && !options.scene.birds &&
                        options.scene.dayNight.dayLengthSeconds == 0, "Scenario and session settings did not compose");
                // Recheck the current file when starting, rather than trusting the catalog.
                write(directory / "idle.toml", "invalid = true\n");
                rejects([&] { setup.options(); }, "Changed scenario was not revalidated");
            } else {
                require(!setup.scenarios()[i].error.empty(), "Unavailable scenario has no explanation");
                rejects([&] { setup.options(); }, "Broken or unprepared scenario was accepted");
            }
        }
        setup.restoreOptions();
        require(setup.birds && setup.startHour == 7.25 && setup.dayLengthSeconds == 3600 && setup.port == "65535",
                "Restore options changed connection settings or lost launch defaults");
        multiplayer::SessionSettings settings;
        settings.zombieCount = 1234; settings.spawn = glm::vec2(12, -34); settings.look = {45, -20};
        settings.startHour = 23.99999999; settings.dayLengthSeconds = 0;
        settings.birds = true; settings.trees = false; settings.vegetation = 4;
        const auto bytes = multiplayer::encode(settings);
        const auto decoded = std::get<multiplayer::SessionSettings>(multiplayer::decode(bytes.data(), bytes.size()));
        require(decoded.zombieCount == 1234 && decoded.spawn == settings.spawn && decoded.startHour == settings.startHour &&
                decoded.birds && !decoded.trees && decoded.vegetation == 4, "Settings round trip lost data or time precision");
        for (std::size_t n = 0; n < bytes.size(); ++n)
            rejects([&] { multiplayer::decode(bytes.data(), n); }, "Truncated settings accepted");
        for (const auto count : {100001u, UINT32_MAX}) {
            auto invalid = settings; invalid.zombieCount = count;
            rejects([&] { multiplayer::encode(invalid); }, "Oversized host population accepted");
        }
        auto invalid = settings; invalid.vegetation = 255;
        rejects([&] { multiplayer::encode(invalid); }, "Invalid vegetation accepted");
        invalid = settings; invalid.startHour = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { multiplayer::encode(invalid); }, "Invalid clock accepted");
        const auto applied = multiplayer::applySessionSettings(defaults, decoded);
        require(applied.map == defaults.map && applied.zombieAssets == defaults.zombieAssets &&
                applied.replicaZombieCount == 1234 && applied.look == settings.look && applied.spawn == settings.spawn,
                "Host settings changed local paths or failed to supply scenario population/spawn/look");
        defaults.explicitLook = true; defaults.look = {-90, 0};
        require(multiplayer::applySessionSettings(defaults, decoded).look == defaults.look,
                "Host settings replaced an explicit local camera orientation");
        std::cout << "Host scenario selection, validation, option defaults and session settings passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
