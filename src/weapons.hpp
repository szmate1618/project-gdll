#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace viewer {
// Slots 0..3 correspond to keys 1..4. Ammo counts use rounds (shotgun shells),
// not pellets. Configuration and starting inventory are read once at startup.
struct WeaponConfig {
    std::string id, name, ammoType;
    double shotIntervalSeconds = 0;
    bool automatic = false, hasScope = false;
    int pellets = 1, magazineCapacity = 1, ammoPerShot = 1;
    float spreadDegrees = 0; // Cone half-angle about the camera's forward ray.
};
struct WeaponState {
    WeaponConfig config;
    bool owned = false;
    int loaded = 0, reserve = 0;
    double readyAt = 0;
};

class Weapons {
public:
    Weapons(const std::filesystem::path& items, const std::filesystem::path& inventory);
    bool equip(std::size_t slot);
    std::size_t slot() const { return slot_; }
    const WeaponState& equipped() const { return weapons_[slot_]; }
    void trigger(bool held);
    void cancelTrigger();
    // One firing opportunity per frame, with no burst of overdue shots after a
    // stall. Semi-automatic clicks during cooldown are discarded, not queued.
    bool fire(double seconds);
    int reload(); // Instant for the prototype; returns transferred rounds.
    std::string status() const;
private:
    std::array<WeaponState, 4> weapons_;
    std::size_t slot_ = 0;
    bool held_ = false, pressed_ = false;
};

// Deterministic cone pattern, including a centered pellet. Each direction uses
// the existing zombie hit query; geometry/physics stay outside this module.
std::vector<glm::vec3> weaponShotDirections(const WeaponConfig& config, glm::vec3 forward);
} // namespace viewer
