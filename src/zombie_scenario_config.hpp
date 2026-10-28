#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace viewer {

inline constexpr std::size_t zombieScenarioMaximumCount = 100000;

enum class ZombieScenarioMovement { idle, travel };

struct ZombieHordeConfig {
    using Rectangle = std::array<double, 4>;

    std::string name;
    std::size_t count = 0;
    // [min_x, min_z, max_x, max_z], absolute runtime X east / Z south meters.
    // An omitted rectangle allows valid positions across the map.
    std::optional<Rectangle> spawn;
    std::optional<Rectangle> destination;
    ZombieScenarioMovement movement = ZombieScenarioMovement::idle;
    // Resolved relative to the scenario file by the TOML loader. Empty means uniform.
    std::filesystem::path spawnDensity{};
    std::filesystem::path destinationDensity{};
    // Maximum horizontal adjustment in meters, for uniform and weighted targets.
    float placementSearchRadius = 100.0f;
};

struct ZombieScenarioConfig {
    struct Player {
        std::array<float, 2> spawn; // Absolute runtime X east / Z south meters; ground height is resolved at load.
        std::array<float, 2> orientation; // Yaw/pitch degrees: yaw 0 east, -90 north; positive pitch looks up.
    };
    std::optional<std::uint32_t> seed;
    std::vector<ZombieHordeConfig> hordes;
    std::optional<Player> player;

    // Validates counts and movement settings, including direct C++ callers.
    std::size_t count() const;
    bool needsNavigation() const;
};

// Format version 5 (also the default). Explicit version 4 retains uniform
// sampling without nearby adjustment. All locations use world X/Z meters.
// Throws std::runtime_error with the file path for malformed/unsupported settings.
ZombieScenarioConfig loadZombieScenarioConfig(const std::filesystem::path& path);

} // namespace viewer
