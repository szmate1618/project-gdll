#include "weapons.hpp"
#include "inventory/item_config.hpp"
#include "inventory/player_inventory.hpp"

#include <toml++/toml.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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
    // Initialize the display cache through actual item definitions; descriptor
    // IDs need not equal weapon ammo types. Runtime counts come from syncAmmo.
    std::map<std::string, int> initialAmmo;
    std::map<std::string, ItemInventoryConfig> ammoDefinitions;
    for (const auto& [key, node] : *ammo) {
        const auto* stack = node.as_table();
        if (!stack) throw std::runtime_error(inventory.string() + ": ammo." + std::string(key.str()) + " must be an item stack table");
        const auto item = required<std::string>(*stack, "item", inventory);
        const auto quantity = count(*stack, "quantity", inventory);
        auto definition = ammoDefinitions.find(item);
        if (definition == ammoDefinitions.end())
            definition = ammoDefinitions.emplace(item, loadItem(definitions.parent_path() / "ammo", item).inventory()).first;
        if (definition->second.type != "ammo" || quantity > definition->second.maxStack)
            throw std::runtime_error(inventory.string() + ": invalid ammo item or quantity for " + item);
        auto& total = initialAmmo[definition->second.ammoType];
        if (quantity > std::numeric_limits<int>::max() - total)
            throw std::runtime_error(inventory.string() + ": invalid or excessive ammo stack quantity");
        total += quantity;
    }
    std::set<std::string> ammoTypes;
    bool foundEquipped = false;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto& weapon = weapons_[i];
        weapon.config = loadWeaponConfig(definitions / (std::string(ids[i]) + ".gameplay.toml"), ids[i]);
        if (!ammoTypes.insert(weapon.config.ammoType).second)
            throw std::runtime_error("Prototype weapons require separate ammo_type values");
        weapon.reserve = initialAmmo[weapon.config.ammoType];
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
    if (reloading_ || slot >= weapons_.size() || !weapons_[slot].owned) return false;
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
WeaponFireResult Weapons::fireAttempt(double seconds, const std::function<bool()>& accept) {
    auto& weapon = weapons_[slot_];
    const bool requested = pressed_ || (held_ && weapon.config.automatic);
    pressed_ = false;
    if (reloading_ || !weapon.owned || !requested || !std::isfinite(seconds) || seconds < weapon.readyAt) return WeaponFireResult::none;
    if (weapon.loaded < weapon.config.ammoPerShot) {
        if (emptyClicked_) return WeaponFireResult::none;
        emptyClicked_ = true;
        return WeaponFireResult::empty;
    }
    if (accept && !accept()) return WeaponFireResult::none;
    weapon.loaded -= weapon.config.ammoPerShot;
    weapon.readyAt = seconds + weapon.config.shotIntervalSeconds;
    emptyClicked_ = false;
    return WeaponFireResult::fired;
}
void Weapons::synchronizeOwnership(const PlayerInventory& inventory, std::size_t equipped, int loaded) {
    for (std::size_t i = 0; i < weaponIds.size(); ++i) {
        weapons_[i].owned = inventory.owns(weaponIds[i]);
        if (i == equipped) weapons_[i].loaded = std::clamp(loaded, 0, weapons_[i].config.magazineCapacity);
    }
    if (equipped < weapons_.size()) slot_ = equipped;
    syncAmmo(inventory);
}
void Weapons::syncAmmo(const PlayerInventory& inventory) {
    for (std::size_t i = 0; i < ids.size(); ++i)
        weapons_[i].reserve = inventory.ammoCount(weapons_[i].config.ammoType);
}
std::optional<WeaponItem> Weapons::discard() {
    return discard(slot_);
}
std::optional<WeaponItem> Weapons::discard(std::size_t slot) {
    if (slot >= weaponIds.size()) return {};
    auto& weapon = weapons_[slot];
    if (reloading_ || !weapon.owned) return {};
    const WeaponItem item{slot,weapon.loaded,weapon.readyAt};
    weapon.owned = false;
    weapon.loaded = 0;
    cancelTrigger();
    for (std::size_t offset=1; slot_ == slot && offset<weapons_.size(); ++offset) {
        const auto next = (item.slot+offset)%weapons_.size();
        if (weapons_[next].owned) { slot_ = next; break; }
    }
    return item;
}
bool Weapons::pickUp(const WeaponItem& item) {
    if (reloading_ || item.slot>=weaponIds.size() || weapons_[item.slot].owned || item.loaded<0 ||
        item.loaded>weapons_[item.slot].config.magazineCapacity || !std::isfinite(item.readyAt)) return false;
    auto& weapon = weapons_[item.slot];
    weapon.owned = true;
    weapon.loaded = item.loaded;
    weapon.readyAt = item.readyAt;
    return equip(item.slot);
}
std::string Weapons::status() const {
    if (slot_ == punchSlot) return "Fists";
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
