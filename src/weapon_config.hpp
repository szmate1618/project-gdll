#pragma once

#include "weapon_caliber.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <glm/glm.hpp>

namespace viewer {
struct GrenadeConfig {
    float fuseSeconds = 3, blastRadius = 8, throwSpeed = 14;
};
struct WeaponConfig {
    std::string id, name, ammoType;
    double shotIntervalSeconds = 0;
    bool automatic = false, hasScope = false;
    int pellets = 1, magazineCapacity = 1, ammoPerShot = 1;
    float spreadDegrees = 0; // Cone half-angle about the camera's forward ray.
    ShotCaliber caliber = ShotCaliber::low; // Base damage per bullet/pellet: low 1, high 2.
    glm::vec3 muzzlePosition{0}; // Model-local meters: X right, Y up, muzzle -Z.
    std::optional<GrenadeConfig> grenade; // A thrown body instead of hitscan.
};

// Shared by combat startup and inventory item definitions. No runtime weapon state.
WeaponConfig loadWeaponConfig(const std::filesystem::path& file, const std::string& id);
} // namespace viewer
