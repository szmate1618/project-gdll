#include "remote_weapon_playback.hpp"
#include "camera.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
void RemoteWeaponPlayback::receiveShots(const std::vector<SoundEvent>& shots,
        const std::vector<PlayerState>& players, double serverSeconds) {
    if (players.size() > maxPlayers) throw std::invalid_argument("Remote weapon player budget exceeded");
    for (auto it = players_.begin(); it != players_.end();) {
        const auto player = std::find_if(players.begin(), players.end(),
            [&](const auto& p) { return p.id == it->first; });
        if (player == players.end()) it = players_.erase(it);
        else { it->second.onFoot = player->vehicle == noVehicle; ++it; }
    }
    if (!std::isfinite(serverSeconds)) return;
    for (const auto& shot : shots) {
        const double seconds = static_cast<double>(shot.tick) / 60;
        if (!shot.id || shot.kind != SoundKind::shot || shot.weapon >= 4 || seconds > serverSeconds) continue;
        const auto player = std::find_if(players.begin(), players.end(),
            [&](const auto& p) { return p.id == shot.source; });
        if (player == players.end() || player->vehicle != noVehicle) continue;
        auto& playback = players_[shot.source];
        if (shot.id <= playback.lastShot) continue;
        playback.lastShot = shot.id;
        playback.onFoot = true;
        playback.flashSlot = shot.weapon;
        playback.motion.fire(shot.weapon, seconds);
        playback.flash.fire(seconds, shot.position);
    }
}

std::optional<RemoteWeaponSample> RemoteWeaponPlayback::sample(const PlayerState& player, double seconds) const {
    if (player.vehicle != noVehicle || player.slot >= 4 || player.weaponSwitchRemaining > 0) return {};
    viewer::FPSController body;
    body.restoreMovement(player.movement);
    viewer::Camera eye;
    eye.setPosition(body.eyePosition());
    eye.setOrientation(player.yaw, player.pitch);
    RemoteWeaponSample result;
    result.slot = player.slot;
    result.eyeToWorld = glm::inverse(eye.view());
    const auto playback = players_.find(player.id);
    if (playback != players_.end()) {
        result.motion = playback->second.motion.pose(player.slot, seconds);
        if (playback->second.flashSlot == player.slot) result.flash = playback->second.flash.light(seconds);
    }
    return result;
}

std::vector<viewer::MuzzleFlashLight> RemoteWeaponPlayback::muzzleLights(double seconds) const {
    std::vector<viewer::MuzzleFlashLight> result;
    for (const auto& [id, playback] : players_) {
        (void)id;
        if (!playback.onFoot) continue;
        const auto flash = playback.flash.light(seconds);
        if (glm::any(glm::greaterThan(flash.color, glm::vec3(0)))) result.push_back(flash);
    }
    return result;
}
} // namespace multiplayer
