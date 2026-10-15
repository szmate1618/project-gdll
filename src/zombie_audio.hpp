#pragma once

#include "audio.hpp"
#include <optional>
#include <unordered_map>

namespace viewer {

struct ZombieAudioSample {
    std::size_t id = 0;
    glm::vec3 feet{0};
    double walkSeconds = 0;
    bool walking = false, running = false;
    std::uint64_t heardRevision = 0, chaseRevision = 0;
    double heardSeconds = -1, chaseSeconds = -1;
};
struct AudioCue {
    AudioEffect effect;
    glm::vec3 position{0};
    float gain = 1, pitch = 1;
    std::optional<std::size_t> zombie; // Vocals use the separate capped pool.
    int priority = 1;
};

// Presentation-only deterministic scheduling for the selected nearby zombies.
// No gameplay RNG, hearing propagation, skeleton evaluation or audio-device calls.
class ZombieAudioScheduler {
public:
    std::vector<AudioCue> update(double seconds, const std::vector<ZombieAudioSample>& samples);
    void clear() { states_.clear(); }
private:
    struct State {
        glm::vec3 feet{0};
        double walkSeconds = 0, nextCall = 0, stride = 0, lastSeen = 0;
        std::uint64_t heard = 0, chase = 0, calls = 0, steps = 0;
        bool selected = false;
    };
    std::unordered_map<std::size_t, State> states_;
};
} // namespace viewer
