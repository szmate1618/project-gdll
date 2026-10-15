#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace viewer {

enum class AudioEffect { footstep, landing, pistol, smg, sniper, shotgun, reload, zombie, count };

struct AudioOptions {
    std::filesystem::path directory;
    bool enabled = true;
    bool rain = false;
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
    // Retain the closest voices within 40 m; nearer newcomers replace farther
    // voices. Call with all nearby candidates; order does not affect selection.
    void setZombieEmitters(const std::vector<AudioEmitter>& candidates);
    void update(); // Refill bounded ambience queues and recover after stalls.
    AudioStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
