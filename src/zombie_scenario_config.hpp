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

struct ZombieHordeConfig {
    enum class CoordinateSystem { World, Wgs84 };
    using Rectangle = std::array<double, 4>;

    std::string name;
    std::size_t count = 0;
    CoordinateSystem coordinateSystem = CoordinateSystem::World;
    // World: [min_x, min_z, max_x, max_z], absolute runtime meters.
    // WGS84: [west, south, east, north], longitude/latitude degrees.
    // An omitted rectangle allows valid positions across the map.
    std::optional<Rectangle> spawn;
    std::optional<Rectangle> destination;
};

struct ZombieScenarioConfig {
    std::optional<std::uint32_t> seed;
    std::vector<ZombieHordeConfig> hordes;

    // Validates individual counts and the total, including direct C++ callers.
    std::size_t count() const;
};

// Format version 2; an omitted version also selects version 2.
// Throws std::runtime_error with the file path for malformed/unsupported settings.
ZombieScenarioConfig loadZombieScenarioConfig(const std::filesystem::path& path);

} // namespace viewer
