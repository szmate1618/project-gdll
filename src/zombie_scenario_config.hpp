#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace viewer {

inline constexpr std::size_t zombieScenarioMaximumCount = 100000;

struct ZombieScenarioConfig {
    enum class CoordinateSystem { Meters, Wgs84 };

    std::optional<std::uint32_t> seed;
    std::size_t count = 0;
    CoordinateSystem coordinateSystem = CoordinateSystem::Meters;
    // [west, south, east, north]: local meter offsets or longitude/latitude degrees.
    std::array<double, 4> spawnRectangle{};
};

// Throws std::runtime_error with the file path for malformed/unsupported settings.
ZombieScenarioConfig loadZombieScenarioConfig(const std::filesystem::path& path);

} // namespace viewer
