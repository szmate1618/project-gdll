#include "weapons.hpp"

#include <toml++/toml.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
constexpr std::array<const char*, 4> ids{"pistol", "submachine_gun", "sniper", "shotgun"};

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
WeaponConfig loadConfig(const std::filesystem::path& path, const std::string& id) {
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
    if (config.name.empty() || config.ammoType.empty() ||
        !std::isfinite(config.shotIntervalSeconds) || config.shotIntervalSeconds <= 0 ||
        config.pellets < 1 || config.pellets > 64 || config.ammoPerShot < 1 ||
        config.magazineCapacity < config.ammoPerShot || !std::isfinite(spread) || spread < 0 || spread >= 90)
        throw std::runtime_error(path.string() + ": invalid weapon configuration (check counts, interval, and spread)");
    config.spreadDegrees = static_cast<float>(spread);
    return config;
}
} // namespace

Weapons::Weapons(const std::filesystem::path& items, const std::filesystem::path& inventory) {
    const auto table = toml::parse_file(inventory.string());
    const auto* guns = table["weapons"].as_table();
    const auto* ammo = table["ammo"].as_table();
    if (!guns || !ammo) throw std::runtime_error(inventory.string() + ": expected [weapons] and [ammo] tables");
    const auto selected = required<std::string>(table, "equipped", inventory);
    std::set<std::string> ammoTypes;
    bool foundEquipped = false;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto& weapon = weapons_[i];
        weapon.config = loadConfig(items / (std::string(ids[i]) + ".toml"), ids[i]);
        if (!ammoTypes.insert(weapon.config.ammoType).second)
            throw std::runtime_error("Prototype weapons require separate ammo_type values");
        weapon.reserve = count(*ammo, weapon.config.ammoType, inventory);
        weapon.owned = guns->contains(ids[i]);
        if (weapon.owned) weapon.loaded = count(*guns, ids[i], inventory);
        if (weapon.loaded > weapon.config.magazineCapacity)
            throw std::runtime_error(inventory.string() + ": loaded rounds exceed " + weapon.config.id + " capacity");
        if (selected == ids[i] && weapon.owned) { slot_ = i; foundEquipped = true; }
    }
    for (const auto& [key, value] : *guns) {
        (void)value;
        if (std::none_of(ids.begin(), ids.end(), [&](const char* id) { return key.str() == id; }))
            throw std::runtime_error(inventory.string() + ": unknown weapon " + std::string(key.str()));
    }
    if (!foundEquipped) throw std::runtime_error(inventory.string() + ": equipped must name an owned weapon");
}

bool Weapons::equip(std::size_t slot) {
    if (slot >= weapons_.size() || !weapons_[slot].owned) return false;
    slot_ = slot;
    cancelTrigger();
    return true;
}
void Weapons::trigger(bool held) {
    if (held && !held_) pressed_ = true;
    held_ = held;
}
void Weapons::cancelTrigger() { held_ = pressed_ = false; }
bool Weapons::fire(double seconds) {
    auto& weapon = weapons_[slot_];
    const bool requested = pressed_ || (held_ && weapon.config.automatic);
    pressed_ = false;
    if (!requested || !std::isfinite(seconds) || seconds < weapon.readyAt || weapon.loaded < weapon.config.ammoPerShot)
        return false;
    weapon.loaded -= weapon.config.ammoPerShot;
    weapon.readyAt = seconds + weapon.config.shotIntervalSeconds;
    return true;
}
int Weapons::reload() {
    auto& weapon = weapons_[slot_];
    const int rounds = std::min(weapon.config.magazineCapacity - weapon.loaded, weapon.reserve);
    weapon.loaded += rounds;
    weapon.reserve -= rounds;
    cancelTrigger();
    return rounds;
}
std::string Weapons::status() const {
    const auto& weapon = equipped();
    return weapon.config.name + " " + std::to_string(weapon.loaded) + "/" +
        std::to_string(weapon.config.magazineCapacity) + " | reserve " + std::to_string(weapon.reserve);
}

std::vector<glm::vec3> weaponShotDirections(const WeaponConfig& config, glm::vec3 forward) {
    forward = glm::normalize(forward);
    std::vector<glm::vec3> result{forward};
    const auto reference = std::abs(forward.y) > .99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const auto right = glm::normalize(glm::cross(forward, reference));
    const auto up = glm::cross(right, forward);
    const float radius = std::tan(glm::radians(config.spreadDegrees));
    for (int i = 1; i < config.pellets; ++i) {
        const float angle = 2 * glm::pi<float>() * (i - 1) / (config.pellets - 1);
        result.push_back(glm::normalize(forward + radius * (std::cos(angle) * right + std::sin(angle) * up)));
    }
    return result;
}
} // namespace viewer
