#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace viewer {

enum class AudioEffect {
    footstep, landing, pistol, smg, sniper, shotgun, reload, zombie,
    footstep2, footstep3, footstep4, zombie2, zombie3,
    huh, intrigued, chase, chase2, doorLatch, doorSlam, emptyClick,
    reloadRifle, reloadShotgun, explosion, rustle, rustle2, rustle3, dryRustle, dryRustle2, twigSnap, count
};

struct AudioOptions {
    std::filesystem::path directory;
    bool enabled = true;
    bool rain = false;
    bool ambience = true; // Multiplayer currently uses only local shrub effects.
    bool hrtf = true;
    float gain = 0.6f;
    std::size_t effectVoices = 24, zombieVoices = 8;
};

struct AudioEmitter {
    std::size_t id = 0; // Stable gameplay ID; has no ownership of the character.
    glm::vec3 position{0}; // X east, Y up, Z south, meters.
};

struct AudioStats {
    std::size_t effects = 0;
    std::vector<std::size_t> zombies;
    std::vector<std::size_t> zombieEmitters;
    std::size_t streamBuffers = 0;
    std::size_t playingStreams = 0;
    std::uint64_t streamFramesRead = 0;
};

// Owns one OpenAL device/context and all its sources/buffers. Construct, update
// and destroy on the application thread; OpenAL owns the mixing thread.
// No dependency on simulation noise, hearing thresholds, or render visibility.
class AudioSystem {
public:
    explicit AudioSystem(const AudioOptions& options);
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    bool enabled() const;
    bool hrtfEnabled() const;
    void setListener(glm::vec3 eye, glm::vec3 forward, glm::vec3 up = {0, 1, 0});
    void play(AudioEffect effect, glm::vec3 position, float gain = 1, float pitch = 1);
    // Own-player weapon/reload sounds are attached to the listener.
    void playLocal(AudioEffect effect, float gain = 1);
    // Reserve the closest voices within 40 m and update their positions. This
    // does not start playback. Return selected IDs in nearest-first order.
    std::vector<std::size_t> setZombieEmitters(const std::vector<AudioEmitter>& candidates);
    // One-shot vocals share the bounded zombie pool. Reactions can interrupt
    // ordinary moans; ordinary moans never interrupt a hearing/chase reaction.
    bool playZombie(std::size_t id, AudioEffect effect, int priority = 1,
                    float gain = 0.7f, float pitch = 1);
    void update(); // Refill bounded ambience queues and recover after stalls.
    AudioStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
