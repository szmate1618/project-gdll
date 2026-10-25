#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <optional>
#include <cstdint>
#include "weapon_config.hpp"

namespace viewer {
inline constexpr std::array<const char*, 5> weaponIds{"pistol", "submachine_gun", "shotgun", "sniper", "he_grenade"};
constexpr std::size_t punchSlot = weaponIds.size(); // Equipment ID; hotbar positions are independent.
// Slots 0..4 are fixed weapon IDs. Ammo counts use rounds (shotgun shells),
// not pellets. Configuration and starting inventory are read once at startup.
struct WeaponState {
    WeaponConfig config;
    bool owned = false;
    int loaded = 0, reserve = 0;
    double readyAt = 0;
};
enum class WeaponFireResult { none, fired, empty };
// A transferable weapon carries its magazine and cooldown. Reserve ammo stays
// in the player's inventory; definitions remain shared by the fixed slot ID.
struct WeaponItem {
    std::size_t slot = 0;
    int loaded = 0;
    double readyAt = 0;
};

class Weapons {
public:
    Weapons(const std::filesystem::path& definitions, const std::filesystem::path& inventory, bool enableMelee = false);
    bool equip(std::size_t slot);
    std::size_t slot() const { return slot_; }
    const WeaponState& equipped() const { return weapons_[slot_]; }
    const WeaponState& state(std::size_t slot) const { return weapons_.at(slot); }
    void trigger(bool held);
    void cancelTrigger();
    // One firing opportunity per frame, with no burst of overdue shots after a
    // stall. Semi-automatic clicks during cooldown are discarded, not queued.
    bool fire(double seconds);
    // Empty triggers report once per hold, after the normal shot cooldown.
    WeaponFireResult fireAttempt(double seconds);
    int reload(); // Instant for the prototype; returns transferred rounds.
    std::optional<WeaponItem> discard();
    bool pickUp(const WeaponItem& item); // Reject an already-owned weapon type.
    std::string status() const;
private:
    std::array<WeaponState, weaponIds.size() + 1> weapons_;
    std::size_t slot_ = 0;
    bool held_ = false, pressed_ = false;
    bool emptyClicked_ = false;
};

// Deterministic cone pattern, including a centered pellet. Each direction uses
// the existing zombie hit query; geometry/physics stay outside this module.
std::vector<glm::vec3> weaponShotDirections(const WeaponConfig& config, glm::vec3 forward);
// Thrown grenades leave the hand until another grenade is loaded and ready.
// Firearms retain their held model during cooldown and when empty.
bool weaponViewVisible(const WeaponConfig& config, std::uint32_t loaded, bool coolingDown);
} // namespace viewer
