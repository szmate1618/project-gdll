#pragma once
#include "sound_cues.hpp"
#include "weapon_audio.hpp"
#include "scene.hpp"
#include "snapshots.hpp"
#include "door_audio.hpp"
#include "undergrowth_audio.hpp"
#include "camera.hpp"
#include <deque>

namespace multiplayer {
// All playback stays on the window thread. No method emits gameplay/AI noise.
class ClientAudio {
public:
    explicit ClientAudio(const viewer::AudioOptions& options) : system_(options) {}
    bool enabled() const { return system_.enabled(); }
    void receive(const Snapshot& snapshot, std::uint32_t self);
    void weaponCommand(const Command& command, double now, const viewer::Weapons& definitions,
                       const viewer::PlayerInventory& inventory);
    void localMovement(const viewer::FPSController& controller, const viewer::UndergrowthMotion& before,
                       const viewer::UndergrowthIndex& shrubs, bool running, std::uint32_t self);
    void localDoor(const Command& command, const viewer::Camera& camera, const Scene& scene);
    void update(const viewer::Camera& camera, const Scene& scene, const Snapshots& snapshots,
                std::uint32_t self, float seconds, double now);
    viewer::AudioStats stats() const { return system_.stats(); }
private:
    void movementSounds(const std::vector<viewer::NoiseEvent>& noises);
    void remoteMovement(const Scene& scene, const Snapshots& snapshots, std::uint32_t self, float seconds, glm::vec3 eye);
    void zombieSounds(const Scene& scene, const Snapshots& snapshots, glm::vec3 eye, double now);
    struct RemotePlayer {
        viewer::FPSState previous;
        viewer::PlayerNoiseEmitter steps;
        viewer::UndergrowthEmitter shrubs;
        bool ready = false;
    };
    struct Reaction { glm::vec3 position; double expires; };
    viewer::AudioSystem system_;
    WeaponAudioPrediction weapons_;
    RemoteSoundCues events_;
    viewer::UndergrowthEmitter localShrubs_;
    viewer::DoorAudioTracker doors_;
    viewer::ZombieAudioScheduler zombies_;
    std::map<std::uint32_t, RemotePlayer> players_;
    std::map<std::size_t, Reaction> reactions_;
    std::deque<viewer::AudioCue> pending_;
    std::vector<std::uint32_t> sampledZombies_;
    std::uint32_t random_ = 0x62756473, localPlayer_ = 0;
    std::size_t step_ = 0;
    double nextSample_ = 0;
    bool doorHeld_ = false;
};
} // namespace multiplayer
