#pragma once

#include "audio.hpp"

namespace viewer {
class CameraRig;
struct ZombieLayer;

// Routes gameplay actions to playback. Owns no AI state and never emits AI noise.
class SceneAudio {
public:
    explicit SceneAudio(const AudioOptions& options) : audio_(options) {}
    void update(const CameraRig& rig, ZombieLayer* zombies, float seconds);
    void shot(std::size_t weaponSlot);
    void reload() { audio_.playLocal(AudioEffect::reload, 0.45f); }
    void service() { audio_.update(); }
    AudioStats stats() const { return audio_.stats(); }

private:
    AudioSystem audio_;
    std::vector<std::size_t> nearbyZombies_;
    std::vector<AudioEmitter> emitters_;
    float queryRemaining_ = 0;
    std::size_t footstep_ = 0;
};
} // namespace viewer
