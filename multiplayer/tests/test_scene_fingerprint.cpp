#include "scene.hpp"
#include <fstream>
#include <iostream>
#include <locale>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct GroupedNumbers : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\1"; }
};
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Fixture directory argument missing");
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const auto first = directory / "first.txt", second = directory / "second.txt";
        for (const auto& path : {first, second}) {
            std::ofstream file(path, std::ios::binary);
            file << "abc";
            if (!file) throw std::runtime_error("Cannot write fingerprint fixture");
        }
        for (const auto* mode : {"osm-tuned2", "orthophoto", "osm", "sentinel-astra", "none"}) {
            const char* args[] = {"client", "--vegetation", mode};
            const auto selected = multiplayer::parseOptions(3, const_cast<char**>(args), false);
            require(selected.vegetation.explicitSelection, "Multiplayer ignored vegetation selection");
            const char* repeated[] = {"server", "--vegetation", mode, "--vegetation", "osm"};
            bool rejected = false;
            try { (void)multiplayer::parseOptions(5, const_cast<char**>(repeated), true); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Multiplayer accepted conflicting vegetation presets");
        }
        multiplayer::Options options;
        for (const bool server : {false, true}) {
            const char* args[] = {"game", "--no-cars"};
            require(!multiplayer::parseOptions(2, const_cast<char**>(args), server).cars,
                    "Client/server ignored --no-cars");
        }
        options.forests = false;
        const glm::vec3 spawn{1.5f, -2.25f, 0};
        const auto fingerprint = multiplayer::sceneFingerprint(options, 7, spawn, {first});
        // Independently calculated for protocol 31: SHA-256 of <II6f2B
        // little-endian settings followed by ASCII SHA-256("abc") in hex.
        require(fingerprint == "e1a02dc72f9a6a1f4e8937415510e281d3857e59636a5b8594f826f093b3e3e6",
                "Scene fingerprint differs from the cross-platform reference");
        require(multiplayer::sceneFingerprint(options, 7, spawn, {second}) == fingerprint,
                "Resource paths changed the fingerprint");
        const auto previous = std::locale::global(std::locale(std::locale::classic(), new GroupedNumbers));
        const auto localized = multiplayer::sceneFingerprint(options, 7, spawn, {first});
        std::locale::global(previous);
        require(localized == fingerprint, "Locale changed the scene fingerprint");
        require(multiplayer::sceneFingerprint(options, 7, {1.5f, -2.25f, -0.0f}, {first}) == fingerprint,
                "Signed zero changed the scene fingerprint");
        auto changed = options;
        changed.display.fullscreen = true;
        changed.display.resolution = glm::ivec2(960, 540);
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) == fingerprint,
                "Local display settings changed the shared scene fingerprint");
        for (const char* value : {"960x540", "1280x720", "1920x1080"}) {
            const char* args[] = {"client", "--fullscreen", "--resolution", value};
            require(multiplayer::parseOptions(4, const_cast<char**>(args), false).display.resolution.has_value(),
                    "A supported resolution was rejected");
        }
        for (const char* value : {"", "0x0", "960X540", "1280x720junk", "800x600"}) {
            const char* args[] = {"client", "--resolution", value};
            bool rejected = false;
            try { (void)multiplayer::parseOptions(3, const_cast<char**>(args), false); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Invalid render resolution was accepted");
        }
        changed = options;
        changed.cars = false;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) != fingerprint,
                "Disabled cars were not fingerprinted without a sidecar");
        changed = options;
        changed.zombieRadius = 101;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) != fingerprint, "Radius was not fingerprinted");
        require(multiplayer::sceneFingerprint(options, 8, spawn, {first}) != fingerprint, "Count was not fingerprinted");
        require(multiplayer::sceneFingerprint(options, 7, {1.5f, -2.25f, 1}, {first}) != fingerprint, "Spawn was not fingerprinted");
        changed = options; changed.trees = false;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) != fingerprint, "Tree setting was not fingerprinted");
        changed = options; changed.forests = true;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) != fingerprint, "Forest setting was not fingerprinted");
        changed = options; changed.dayNight.startHour = 0;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) != fingerprint, "Starting hour was not fingerprinted");
        changed = options; changed.dayNight.dayLengthSeconds = 0;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) != fingerprint, "Day length was not fingerprinted");
        changed = options;
        changed.vegetation.select("none");
        auto disabled = options;
        disabled.trees = disabled.forests = false;
        require(multiplayer::sceneFingerprint(changed, 7, spawn, {first}) ==
                multiplayer::sceneFingerprint(disabled, 7, spawn, {first}),
                "None preset must fingerprint the effective disabled vegetation");
        options.zombieRadius = 999.25f; options.trees = false; options.forests = true;
        require(multiplayer::sceneFingerprint(options, 1024, {0, .1f, -123.5f}, {first}) ==
                "00cee9926339a36d7e2fba9b4fc0765ca3e0e9d64890beee675b91c8e7f78760",
                "Fractional settings differ from the cross-platform reference");
        std::ofstream(second, std::ios::binary) << "abd";
        require(multiplayer::sceneFingerprint(options, 1024, {0, .1f, -123.5f}, {second}) !=
                multiplayer::sceneFingerprint(options, 1024, {0, .1f, -123.5f}, {first}), "Resource content was not fingerprinted");
        std::cout << "Scene fingerprint checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
