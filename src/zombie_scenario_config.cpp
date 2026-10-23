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
               const std::filesystem::path& path, const std::string& prefix = "") {
    for (const auto& [key, value] : table) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end())
            invalid(path, "unknown scenario setting '" + prefix + std::string(key.str()) + "'");
    }
}

std::int64_t requiredInteger(const toml::node* node,
                             const std::filesystem::path& path, const std::string& key) {
    if (!node || !node->is_integer()) invalid(path, key + " must be an integer");
    return *node->value<std::int64_t>();
}

std::array<float, 2> playerPair(const toml::table& table, const char* key, const std::filesystem::path& path) {
    const auto* values = table[key].as_array();
    if (!values || values->size() != 2)
        invalid(path, std::string("player.") + key + " must contain two finite numbers");
    std::array<float, 2> result;
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        const auto& node = (*values)[axis];
        const auto value = node.value<double>();
        if ((!node.is_integer() && !node.is_floating_point()) || !value ||
            !std::isfinite(*value) || std::abs(*value) > 1e7)
            invalid(path, std::string("player.") + key + " contains an invalid coordinate");
        result[axis] = static_cast<float>(*value);
    }
    return result;
}

std::optional<ZombieHordeConfig::Rectangle> rectangle(const toml::table& table, const char* key,
    ZombieHordeConfig::CoordinateSystem coordinates, const std::filesystem::path& path,
    const std::string& prefix) {
    if (!table.contains(key)) return std::nullopt;
    const auto setting = prefix + key;
    const auto* values = table[key].as_array();
    ZombieHordeConfig::Rectangle bounds;
    if (!values || values->size() != bounds.size())
        invalid(path, setting + " must contain four rectangle coordinates");
    for (std::size_t axis = 0; axis < bounds.size(); ++axis) {
        const auto& node = (*values)[axis];
        const auto coordinate = node.value<double>();
        if ((!node.is_integer() && !node.is_floating_point()) || !coordinate ||
            !std::isfinite(*coordinate) || std::abs(*coordinate) > std::numeric_limits<float>::max() / 4.0)
            invalid(path, setting + " must contain four finite coordinates");
        bounds[axis] = *coordinate;
    }
    if (bounds[0] >= bounds[2] || bounds[1] >= bounds[3])
        invalid(path, setting + " requires minimum bounds smaller than maximum bounds");
    if (coordinates == ZombieHordeConfig::CoordinateSystem::Wgs84 &&
        (bounds[0] < -180 || bounds[2] > 180 || bounds[1] < -90 || bounds[3] > 90))
        invalid(path, setting + " must use longitude [-180,180] and latitude [-90,90] degrees");
    return bounds;
}

} // namespace

std::size_t ZombieScenarioConfig::count() const {
    if (hordes.empty()) throw std::invalid_argument("Zombie scenario requires at least one named horde");
    std::size_t total = 0;
    for (const auto& horde : hordes) {
        if (!horde.count || horde.count > zombieScenarioMaximumCount)
            throw std::invalid_argument("Zombie horde '" + horde.name + "' count must be between 1 and 100000");
        if (horde.count > zombieScenarioMaximumCount - total)
            throw std::invalid_argument("Zombie scenario total count must not exceed 100000");
        total += horde.count;
    }
    return total;
}

ZombieScenarioConfig loadZombieScenarioConfig(const std::filesystem::path& path) {
    toml::table document;
    try {
        document = toml::parse_file(path.string());
    } catch (const toml::parse_error& error) {
        invalid(path, std::string("invalid scenario TOML: ") + error.what());
    }
    checkKeys(document, {"version", "seed", "zombies", "player"}, path);
    if (document.contains("version") && requiredInteger(document.get("version"), path, "version") != 2)
        invalid(path, "unsupported scenario version; expected version = 2");
    const auto* zombies = document["zombies"].as_table();
    if (!zombies || zombies->empty()) invalid(path, "expected at least one [zombies.HORDE_NAME] table");

    ZombieScenarioConfig config;
    if (document.contains("player")) {
        const auto* player = document["player"].as_table();
        if (!player) invalid(path, "player must be a [player] table");
        checkKeys(*player, {"spawn", "orientation"}, path, "player.");
        const auto spawn = playerPair(*player, "spawn", path);
        const auto orientation = playerPair(*player, "orientation", path);
        if (std::abs(orientation[0]) > 180 || std::abs(orientation[1]) > 89)
            invalid(path, "player.orientation requires yaw [-180,180] and pitch [-89,89] degrees");
        config.player = ZombieScenarioConfig::Player{spawn, orientation};
    }
    if (document.contains("seed")) {
        const auto seed = requiredInteger(document.get("seed"), path, "seed");
        if (seed < 0 || seed > std::numeric_limits<std::uint32_t>::max())
            invalid(path, "seed must be between 0 and 4294967295");
        config.seed = static_cast<std::uint32_t>(seed);
    }
    for (const auto& [name, node] : *zombies) {
        const auto* table = node.as_table();
        if (!table || name.str().empty())
            invalid(path, "expected named [zombies.HORDE_NAME] tables; migrate old flat zombie settings to version 2");
        const auto prefix = "zombies." + std::string(name.str()) + ".";
        checkKeys(*table, {"count", "coordinates", "spawn", "destination"}, path, prefix);
        ZombieHordeConfig horde;
        horde.name = name.str();
        const auto count = requiredInteger(table->get("count"), path, prefix + "count");
        if (count < 1 || static_cast<std::uint64_t>(count) > zombieScenarioMaximumCount)
            invalid(path, prefix + "count must be between 1 and 100000");
        horde.count = static_cast<std::size_t>(count);
        if (table->contains("coordinates")) {
            const auto coordinates = (*table)["coordinates"].value<std::string>();
            if (coordinates == std::optional<std::string>("wgs84"))
                horde.coordinateSystem = ZombieHordeConfig::CoordinateSystem::Wgs84;
            else if (coordinates != std::optional<std::string>("world"))
                invalid(path, prefix + "coordinates must be 'world' or 'wgs84'");
        }
        horde.spawn = rectangle(*table, "spawn", horde.coordinateSystem, path, prefix);
        horde.destination = rectangle(*table, "destination", horde.coordinateSystem, path, prefix);
        config.hordes.push_back(std::move(horde));
    }
    try { (void)config.count(); }
    catch (const std::invalid_argument& error) { invalid(path, error.what()); }
    return config;
}

} // namespace viewer
