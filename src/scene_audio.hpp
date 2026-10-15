#pragma once

#include "audio.hpp"
#include "door_audio.hpp"
#include "zombie_audio.hpp"

namespace viewer {
class CameraRig;
class DoorLayer;
struct ZombieLayer;

// Routes gameplay actions to playback. Owns no AI state and never emits AI noise.
class SceneAudio {
public:
    explicit SceneAudio(const AudioOptions& options) : audio_(options) {}
    void update(const CameraRig& rig, ZombieLayer* zombies, float seconds, const DoorLayer* doors = nullptr);
    void shot(std::size_t weaponSlot);
    void reload(std::size_t weaponSlot = 0);
    void emptyClick() { audio_.playLocal(AudioEffect::emptyClick, .5f); }
    void grabDoor(const DoorLayer& doors, std::size_t id);
    void service() { audio_.update(); }
    AudioStats stats() const { return audio_.stats(); }

private:
    AudioSystem audio_;
    std::vector<std::size_t> nearbyZombies_;
    std::vector<AudioEmitter> emitters_;
    std::vector<ZombieAudioSample> zombieSamples_;
    ZombieAudioScheduler zombieScheduler_;
    ZombieLayer* zombieLayer_ = nullptr; // Identity only; never dereferenced after update.
    DoorAudioTracker doorTracker_;
    float queryRemaining_ = 0;
    std::size_t footstep_ = 0;
};
} // namespace viewer
