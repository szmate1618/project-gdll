#pragma once

#include "protocol.hpp"
#include "weapon_motion.hpp"
#include "muzzle_flash.hpp"
#include <map>
#include <optional>

namespace multiplayer {
struct RemoteWeaponSample {
    std::uint8_t slot = 0;
    glm::mat4 eyeToWorld{1}; // Player's camera frame, X right/Y up/forward -Z, in world meters.
    viewer::WeaponMotionPose motion;
    viewer::MuzzleFlashLight flash; // World coordinates.
};

// Pure presentation state shared by remote weapon drawing and scene lighting.
// Shot events are delivered when their tick reaches the buffered render clock;
// no ammunition, health, collision or authoritative firing state changes here.
class RemoteWeaponPlayback {
public:
    void receiveShots(const std::vector<SoundEvent>& shots, const std::vector<PlayerState>& players,
                      double serverSeconds);
    std::optional<RemoteWeaponSample> sample(const PlayerState& player, double serverSeconds) const;
    std::vector<viewer::MuzzleFlashLight> muzzleLights(double serverSeconds) const;
private:
    struct Entry {
        viewer::WeaponMotion motion;
        viewer::MuzzleFlash flash;
        std::uint64_t lastShot = 0;
        std::uint8_t flashSlot = 0;
        bool onFoot = false;
    };
    std::map<std::uint32_t, Entry> players_;
};
} // namespace multiplayer
