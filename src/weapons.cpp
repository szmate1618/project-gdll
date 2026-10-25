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
constexpr auto ids = weaponIds;

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
} // namespace

Weapons::Weapons(const std::filesystem::path& definitions, const std::filesystem::path& inventory, bool enableMelee) {
    auto& punch = weapons_[punchSlot];
    punch.owned = enableMelee;
    punch.config.id = "punch"; punch.config.name = "Fists";
    punch.config.ammoPerShot = 0; punch.config.magazineCapacity = 0;
    punch.config.shotIntervalSeconds = .5;
    const auto table = toml::parse_file(inventory.string());
    const auto* guns = table["weapons"].as_table();
    const auto* ammo = table["ammo"].as_table();
    if (!guns || !ammo) throw std::runtime_error(inventory.string() + ": expected [weapons] and [ammo] tables");
    const auto selected = required<std::string>(table, "equipped", inventory);
    std::set<std::string> ammoTypes;
    bool foundEquipped = false;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto& weapon = weapons_[i];
        weapon.config = loadConfig(definitions / (std::string(ids[i]) + ".toml"), ids[i]);
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
    if (!held) emptyClicked_ = false;
    held_ = held;
}
void Weapons::cancelTrigger() { held_ = pressed_ = emptyClicked_ = false; }
bool Weapons::fire(double seconds) {
    return fireAttempt(seconds) == WeaponFireResult::fired;
}
WeaponFireResult Weapons::fireAttempt(double seconds) {
    auto& weapon = weapons_[slot_];
    const bool requested = pressed_ || (held_ && weapon.config.automatic);
    pressed_ = false;
    if (!weapon.owned || !requested || !std::isfinite(seconds) || seconds < weapon.readyAt) return WeaponFireResult::none;
    if (weapon.loaded < weapon.config.ammoPerShot) {
        if (emptyClicked_) return WeaponFireResult::none;
        emptyClicked_ = true;
        return WeaponFireResult::empty;
    }
    weapon.loaded -= weapon.config.ammoPerShot;
    weapon.readyAt = seconds + weapon.config.shotIntervalSeconds;
    emptyClicked_ = false;
    return WeaponFireResult::fired;
}
int Weapons::reload() {
    auto& weapon = weapons_[slot_];
    cancelTrigger();
    if (!weapon.owned) return 0;
    const int rounds = std::min(weapon.config.magazineCapacity - weapon.loaded, weapon.reserve);
    weapon.loaded += rounds;
    weapon.reserve -= rounds;
    return rounds;
}
std::optional<WeaponItem> Weapons::discard() {
    auto& weapon = weapons_[slot_];
    if (!weapon.owned || slot_ == punchSlot) return {};
    const WeaponItem item{slot_,weapon.loaded,weapon.readyAt};
    weapon.owned = false;
    weapon.loaded = 0;
    cancelTrigger();
    for (std::size_t offset=1; offset<weapons_.size(); ++offset) {
        const auto next = (item.slot+offset)%weapons_.size();
        if (weapons_[next].owned) { slot_ = next; break; }
    }
    return item;
}
bool Weapons::pickUp(const WeaponItem& item) {
    if (item.slot>=weaponIds.size() || weapons_[item.slot].owned || item.loaded<0 ||
        item.loaded>weapons_[item.slot].config.magazineCapacity || !std::isfinite(item.readyAt)) return false;
    auto& weapon = weapons_[item.slot];
    weapon.owned = true;
    weapon.loaded = item.loaded;
    weapon.readyAt = item.readyAt;
    return equip(item.slot);
}
std::string Weapons::status() const {
    if (slot_ == punchSlot) return "Fists (key left of 1; HU 0)";
    const auto& weapon = equipped();
    if (!weapon.owned) return "Unarmed";
    return weapon.config.name + " " + std::to_string(weapon.loaded) + "/" +
        std::to_string(weapon.config.magazineCapacity) + " | reserve " + std::to_string(weapon.reserve);
}

bool weaponViewVisible(const WeaponConfig& config, std::uint32_t loaded, bool coolingDown) {
    return !config.grenade || (!coolingDown && loaded >= static_cast<std::uint32_t>(config.ammoPerShot));
}

std::vector<glm::vec3> weaponShotDirections(const WeaponConfig& config, glm::vec3 forward) {
    if (config.grenade || config.id == "punch") return {};
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
