#include "zombie_scenario_config.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace viewer {
namespace {

[[noreturn]] void invalid(const std::filesystem::path& path, const std::string& detail) {
    throw std::runtime_error(path.string() + ": " + detail);
}

void checkKeys(const toml::table& table, std::initializer_list<std::string_view> allowed,
               const std::filesystem::path& path) {
    for (const auto& [key, value] : table) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end())
            invalid(path, "unknown scenario setting '" + std::string(key.str()) + "'");
    }
}

std::int64_t requiredInteger(const toml::node* node,
                             const std::filesystem::path& path, const char* key) {
    if (!node || !node->is_integer()) invalid(path, std::string(key) + " must be an integer");
    return *node->value<std::int64_t>();
}

void requireRandom(const toml::table& table, const char* key,
                   const std::filesystem::path& path) {
    if (table[key].value<std::string>() != std::optional<std::string>("random"))
        invalid(path, std::string("zombies.") + key + " must be 'random'");
}

} // namespace

ZombieScenarioConfig loadZombieScenarioConfig(const std::filesystem::path& path) {
    toml::table document;
    try {
        document = toml::parse_file(path.string());
    } catch (const toml::parse_error& error) {
        invalid(path, std::string("invalid scenario TOML: ") + error.what());
    }
    checkKeys(document, {"seed", "zombies"}, path);
    const auto* zombies = document["zombies"].as_table();
    if (!zombies) invalid(path, "expected a [zombies] table");
    checkKeys(*zombies, {"count", "spawn", "destination", "spawn_rectangle_m",
                         "spawn_rectangle_wgs84"}, path);

    ZombieScenarioConfig config;
    if (document.contains("seed")) {
        const auto seed = requiredInteger(document.get("seed"), path, "seed");
        if (seed < 0 || seed > std::numeric_limits<std::uint32_t>::max())
            invalid(path, "seed must be between 0 and 4294967295");
        config.seed = static_cast<std::uint32_t>(seed);
    }
    const auto count = requiredInteger(zombies->get("count"), path, "zombies.count");
    if (count < 1 || static_cast<std::uint64_t>(count) > zombieScenarioMaximumCount)
        invalid(path, "zombies.count must be between 1 and 100000");
    config.count = static_cast<std::size_t>(count);
    requireRandom(*zombies, "spawn", path);
    requireRandom(*zombies, "destination", path);

    const bool meters = zombies->contains("spawn_rectangle_m");
    const bool wgs84 = zombies->contains("spawn_rectangle_wgs84");
    if (meters == wgs84)
        invalid(path, "specify exactly one of zombies.spawn_rectangle_m or zombies.spawn_rectangle_wgs84");
    config.coordinateSystem = meters ? ZombieScenarioConfig::CoordinateSystem::Meters
                                     : ZombieScenarioConfig::CoordinateSystem::Wgs84;
    const auto key = meters ? "spawn_rectangle_m" : "spawn_rectangle_wgs84";
    const auto* rectangle = (*zombies)[key].as_array();
    if (!rectangle || rectangle->size() != config.spawnRectangle.size())
        invalid(path, std::string("zombies.") + key + " must contain [west, south, east, north]");
    for (std::size_t axis = 0; axis < config.spawnRectangle.size(); ++axis) {
        const auto& node = (*rectangle)[axis];
        const auto coordinate = node.value<double>();
        if ((!node.is_integer() && !node.is_floating_point()) || !coordinate ||
            !std::isfinite(*coordinate) || std::abs(*coordinate) > std::numeric_limits<float>::max() / 4.0)
            invalid(path, std::string("zombies.") + key + " must contain four finite coordinates");
        config.spawnRectangle[axis] = *coordinate;
    }
    const auto& bounds = config.spawnRectangle;
    if (bounds[0] >= bounds[2] || bounds[1] >= bounds[3])
        invalid(path, std::string("zombies.") + key + " requires west < east and south < north");
    if (wgs84 && (bounds[0] < -180 || bounds[2] > 180 || bounds[1] < -90 || bounds[3] > 90))
        invalid(path, "zombies.spawn_rectangle_wgs84 must use longitude [-180,180] and latitude [-90,90] degrees");
    return config;
}

} // namespace viewer
