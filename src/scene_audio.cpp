#include "scene_audio.hpp"
#include "camera_rig.hpp"
#include "zombie_layer.hpp"

#include <array>
#include <algorithm>
#include <cmath>

namespace viewer {
void SceneAudio::shot(std::size_t weaponSlot) {
    constexpr std::array<AudioEffect, 4> effects{
        AudioEffect::pistol, AudioEffect::smg, AudioEffect::sniper, AudioEffect::shotgun};
    if (weaponSlot < effects.size()) audio_.playLocal(effects[weaponSlot], 0.8f);
}

void SceneAudio::update(const CameraRig& rig, ZombieLayer* zombies, float seconds) {
    if (!audio_.enabled()) return;
    const auto& camera = rig.camera();
    audio_.setListener(camera.position(), camera.direction());
    if (rig.walking()) {
        for (const auto& event : rig.player().noises()) {
            if (event.kind == NoiseEvent::Kind::footstep)
                audio_.play(AudioEffect::footstep, event.position, 0.45f, 0.94f + 0.03f * (footstep_++ % 5));
            else if (event.kind == NoiseEvent::Kind::landing)
                audio_.play(AudioEffect::landing, event.position, 0.65f);
        }
    }
    emitters_.clear();
    if (zombies) {
        queryRemaining_ -= std::isfinite(seconds) ? std::max(seconds, 0.f) : 0.f;
        if (queryRemaining_ <= 0) {
            // Include the 1.5 m voice-height offset; the backend applies its
            // exact 40 m audible radius. This query never scans the population.
            zombies->queryLivingNear(camera.position(), 42, nearbyZombies_);
            queryRemaining_ = 0.2f;
        }
        for (const auto id : nearbyZombies_) {
            if (id >= zombies->instances.size()) continue;
            const auto& zombie = zombies->instances[id];
            if (zombie.ragdoll || zombie.removed) continue;
            emitters_.push_back({id, glm::vec3(zombie.restRoot[3]) + glm::vec3(0, 1.5f, 0)});
        }
    } else {
        nearbyZombies_.clear();
        queryRemaining_ = 0;
    }
    // Positions and deaths update every frame; only the grid query runs at 5 Hz.
    audio_.setZombieEmitters(emitters_);
    service();
}
} // namespace viewer
