#include "scene_audio.hpp"
#include "camera_rig.hpp"
#include "zombie_layer.hpp"
#include "door_layer.hpp"
#include "simulation_clock.hpp"

#include <array>
#include <algorithm>
#include <cmath>

namespace viewer {
void SceneAudio::shot(std::size_t weaponSlot) {
    constexpr std::array<AudioEffect, 4> effects{
        AudioEffect::pistol, AudioEffect::smg, AudioEffect::sniper, AudioEffect::shotgun};
    if (weaponSlot < effects.size()) audio_.playLocal(effects[weaponSlot], 0.8f);
}

void SceneAudio::reload(std::size_t weaponSlot) {
    const auto effect = weaponSlot == 3 ? AudioEffect::reloadShotgun :
                        weaponSlot == 1 || weaponSlot == 2 ? AudioEffect::reloadRifle : AudioEffect::reload;
    audio_.playLocal(effect, .5f);
}

void SceneAudio::grabDoor(const DoorLayer& doors, std::size_t id) {
    if (std::abs(doors.angle(id)) <= .025f)
        audio_.play(AudioEffect::doorLatch, doors.panel(id).center, .6f);
}

void SceneAudio::update(const CameraRig& rig, ZombieLayer* zombies, float seconds, const DoorLayer* doors) {
    if (!audio_.enabled()) return;
    const auto& camera = rig.camera();
    audio_.setListener(camera.position(), camera.direction());
    if (rig.walking()) {
        for (const auto& event : rig.player().noises()) {
            if (event.kind == NoiseEvent::Kind::footstep) {
                constexpr std::array<AudioEffect, 4> steps{AudioEffect::footstep, AudioEffect::footstep2,
                                                         AudioEffect::footstep3, AudioEffect::footstep4};
                const auto step = footstep_++;
                audio_.play(steps[step % steps.size()], event.position, .4f, .97f + .02f * (step % 4));
            } else if (event.kind == NoiseEvent::Kind::landing)
                audio_.play(AudioEffect::landing, event.position, 0.65f);
        }
    }
    emitters_.clear();
    if (zombieLayer_ != zombies) {
        nearbyZombies_.clear();
        queryRemaining_ = 0;
        zombieScheduler_.clear();
        zombieLayer_ = zombies;
    }
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
    const auto selected = audio_.setZombieEmitters(emitters_);
    zombieSamples_.clear();
    for (const auto id : selected) {
        const auto& zombie = zombies->instances[id];
        zombieSamples_.push_back({id, glm::vec3(zombie.restRoot[3]), zombie.walkSeconds,
                                 zombie.walking, zombie.running, zombie.heardNoiseRevision,
                                 zombie.chaseRevision, zombie.heardNoiseSeconds, zombie.chaseSeconds});
    }
    for (const auto& cue : zombieScheduler_.update(SimulationClock::instance().seconds(), zombieSamples_)) {
        if (cue.zombie) audio_.playZombie(*cue.zombie, cue.effect, cue.priority, cue.gain, cue.pitch);
        else audio_.play(cue.effect, cue.position, cue.gain, cue.pitch);
    }
    std::vector<DoorAudioSample> doorSamples;
    if (doors) {
        const auto eye = camera.position();
        for (const auto& door : doors->colliders(eye - glm::vec3(40), eye + glm::vec3(40)))
            doorSamples.push_back({door.door, doors->angle(door.door), door.center});
    }
    for (const auto position : doorTracker_.update(seconds, doorSamples))
        audio_.play(AudioEffect::doorSlam, position, .8f);
    service();
}
} // namespace viewer
