#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <optional>
#include <cstdint>
#include "weapon_config.hpp"
#include "sword_melee.hpp"
#include "inventory/item.hpp"

namespace viewer {
class PlayerInventory;
class WeaponReload;
inline constexpr std::array<const char*, 7> weaponIds{"pistol", "submachine_gun", "shotgun", "sniper", "he_grenade", "molotov", "stake"};
constexpr std::size_t stakeSlot = 6;
constexpr std::size_t handlingSlot = weaponIds.size() + 1;
constexpr std::size_t punchSlot = weaponIds.size(); // Equipment ID; hotbar positions are independent.
constexpr std::size_t swordSlot = weaponIds.size() + 2;
constexpr std::size_t axeSlot = swordSlot + 1;
constexpr std::size_t craftedSlot = axeSlot + 1;
constexpr std::size_t equipmentSlotCount = craftedSlot + 1;
inline bool directionalMeleeSlot(std::size_t slot) { return slot == swordSlot || slot == axeSlot || slot == craftedSlot; }
// Slots 0..6 are fixed equippable item IDs. Ammo counts use rounds (shotgun shells),
// not pellets. Configuration and starting inventory are read once at startup.
struct WeaponState {
    WeaponConfig config;
    bool owned = false;
    int loaded = 0, reserve = 0; // reserve is a display cache of inventory ammo stacks.
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
    SwordMelee& sword() { return sword_; }
    const SwordMelee& sword() const { return sword_; }
    // Directional tools share drag and recovery; combat owns contact policy.
    SwordMelee& axe() { return axe_; }
    const SwordMelee& axe() const { return axe_; }
    SwordMelee& craftedMelee() { return crafted_; }
    const SwordMelee& craftedMelee() const { return crafted_; }
    SwordMelee& directionalMelee() { return slot_ == craftedSlot ? crafted_ : slot_ == axeSlot ? axe_ : sword_; }
    const SwordMelee& directionalMelee() const { return slot_ == craftedSlot ? crafted_ : slot_ == axeSlot ? axe_ : sword_; }
    // The equipped copy retains its saved geometry/grips even when no backpack
    // compartment can hold it. Ground/inventory transactions own acquisition.
    bool equipCrafted(const Item& item);
    const Item* craftedItem() const { return craftedItem_ ? &*craftedItem_ : nullptr; }
    void synchronizeCrafted(const Item* item); // Does not restart an active swing.
    void synchronizeCrafted(const std::optional<Item>& item) { synchronizeCrafted(item ? &*item : nullptr); }
    void clearCrafted();
    // Grenades: tap (<= 0.2 s) drops; hold charges linearly to an automatic
    // full-power throw at 2 s. Uses the same simulation clock as fireAttempt.
    void secondaryTrigger(bool held, double seconds);
    // Launch fraction for the last successful fireAttempt (primary always 1).
    float grenadeThrowPower() const { return grenadeThrowPower_; }
    // Hotbar selection consumes primary input while secondary actions stay usable.
    void cancelPrimaryTrigger();
    void cancelTrigger();
    // One firing opportunity per frame, with no burst of overdue shots after a
    // stall. Semi-automatic clicks during cooldown are discarded, not queued.
    bool fire(double seconds);
    // Empty triggers report once per hold, after the normal shot cooldown.
    // An optional action must accept before ammo/cooldown are committed. It
    // runs only for an eligible trigger and must not change this Weapons object.
    // A rejected semi-automatic press is consumed, not queued.
    WeaponFireResult fireAttempt(double seconds, const std::function<bool()>& accept = {});
    void synchronizeOwnership(const PlayerInventory& inventory, std::size_t equipped, int loaded);
    void syncAmmo(const PlayerInventory& inventory);
    bool reloading() const { return reloading_; }
    std::optional<WeaponItem> discard();
    std::optional<WeaponItem> discard(std::size_t slot);
    bool pickUp(const WeaponItem& item); // Reject an already-owned weapon type.
    std::string status() const;
private:
    friend class WeaponReload;
    std::array<WeaponState, equipmentSlotCount> weapons_;
    SwordMelee sword_, axe_, crafted_;
    std::optional<Item> craftedItem_;
    std::size_t slot_ = 0;
    bool held_ = false, pressed_ = false;
    bool emptyClicked_ = false;
    bool reloading_ = false;
    bool secondaryHeld_ = false; // Survives cancellation until a physical release.
    std::optional<double> grenadeChargeAt_;
    std::optional<float> pendingGrenadePower_;
    float grenadeThrowPower_ = 1;
};

// Deterministic cone pattern, including a centered pellet. Each direction uses
// the existing zombie hit query; geometry/physics stay outside this module.
std::vector<glm::vec3> weaponShotDirections(const WeaponConfig& config, glm::vec3 forward);
// Thrown grenades leave the hand until another grenade is loaded and ready.
// Firearms retain their held model during cooldown and when empty.
bool weaponViewVisible(const WeaponConfig& config, std::uint32_t loaded, bool coolingDown);
} // namespace viewer
