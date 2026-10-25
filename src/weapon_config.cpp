#include "weapon_config.hpp"
#include <toml++/toml.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
template<class T>
T required(const toml::table& table, const std::string& key, const std::filesystem::path& path) {
    const auto value = table[key].value<T>();
    if (!value) throw std::runtime_error(path.string() + ": missing or invalid " + key);
    return *value;
}
int count(const toml::table& table, const std::string& key, const std::filesystem::path& path) {
    const auto value = required<std::int64_t>(table, key, path);
    if (value < 0 || value > std::numeric_limits<int>::max())
        throw std::runtime_error(path.string() + ": " + key + " must be a nonnegative round count");
    return static_cast<int>(value);
}
} // namespace

WeaponConfig loadWeaponConfig(const std::filesystem::path& path, const std::string& id) {
    const auto table = toml::parse_file(path.string());
    WeaponConfig config;
    config.id = id;
    config.name = required<std::string>(table, "name", path);
    config.ammoType = required<std::string>(table, "ammo_type", path);
    config.shotIntervalSeconds = required<double>(table, "shot_interval_seconds", path);
    config.automatic = required<bool>(table, "automatic", path);
    config.hasScope = required<bool>(table, "has_scope", path);
    config.pellets = count(table, "pellets", path);
    config.magazineCapacity = count(table, "magazine_capacity", path);
    config.ammoPerShot = count(table, "ammo_per_shot", path);
    const auto spread = required<double>(table, "spread_degrees", path);
    const auto* muzzle = table["muzzle_position"].as_array();
    if (!muzzle || muzzle->size() != 3)
        throw std::runtime_error(path.string() + ": muzzle_position must contain three finite meter coordinates");
    for (int axis = 0; axis < 3; ++axis) {
        const auto coordinate = (*muzzle)[static_cast<std::size_t>(axis)].value<double>();
        if (!coordinate || !std::isfinite(*coordinate) || std::abs(*coordinate) > std::numeric_limits<float>::max())
            throw std::runtime_error(path.string() + ": invalid muzzle_position coordinate");
        config.muzzlePosition[axis] = static_cast<float>(*coordinate);
    }
    if (config.name.empty() || config.ammoType.empty() ||
        !std::isfinite(config.shotIntervalSeconds) || config.shotIntervalSeconds <= 0 ||
        config.pellets < 1 || config.pellets > 64 || config.ammoPerShot < 1 ||
        config.magazineCapacity < config.ammoPerShot || !std::isfinite(spread) || spread < 0 || spread >= 90)
        throw std::runtime_error(path.string() + ": invalid weapon configuration (check counts, interval, and spread)");
    config.spreadDegrees = static_cast<float>(spread);
    const auto kind = table.contains("kind") ? required<std::string>(table, "kind", path) : "firearm";
    if (kind == "he_grenade") {
        const auto positive = [&](const char* key, double maximum) {
            const auto value = required<double>(table, key, path);
            if (!std::isfinite(value) || value <= 0 || value > maximum)
                throw std::runtime_error(path.string() + ": invalid " + key);
            return static_cast<float>(value);
        };
        config.grenade = GrenadeConfig{positive("fuse_seconds", 30), positive("blast_radius", 50), positive("throw_speed", 30)};
        if (config.automatic || config.pellets != 1 || config.hasScope)
            throw std::runtime_error(path.string() + ": grenades must be single-click, unscoped throws");
    } else if (kind != "firearm") throw std::runtime_error(path.string() + ": unknown weapon kind");
    else {
        const auto caliber = required<std::string>(table, "caliber", path);
        if (caliber == "low") config.caliber = ShotCaliber::low;
        else if (caliber == "high") config.caliber = ShotCaliber::high;
        else throw std::runtime_error(path.string() + ": caliber must be 'low' or 'high'");
    }
    return config;
}
} // namespace viewer
