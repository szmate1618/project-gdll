#include "zombie_audio.hpp"

#include <array>
#include <cmath>
#include <unordered_set>

namespace viewer {
namespace {
// Fixed mixer seed, independent of platform RNG and simulation random choices.
std::uint64_t variation(std::uint64_t id, std::uint64_t sequence) {
    auto value = id + sequence * 0x9e3779b97f4a7c15ULL + 0x517cc1b727220a95ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
bool fresh(double now, double event) { return event >= 0 && event <= now && now - event <= .3; }
}

std::vector<AudioCue> ZombieAudioScheduler::update(double seconds, const std::vector<ZombieAudioSample>& samples) {
    std::vector<AudioCue> cues;
    if (!std::isfinite(seconds)) return cues;
    std::unordered_set<std::size_t> present;
    for (const auto& sample : samples) {
        if (!present.insert(sample.id).second || !std::isfinite(sample.walkSeconds) ||
            !std::isfinite(glm::length(sample.feet))) continue;
        auto inserted = states_.try_emplace(sample.id);
        auto& state = inserted.first->second;
        if (inserted.second) {
            state.feet = sample.feet;
            state.walkSeconds = sample.walkSeconds;
            state.nextCall = seconds + 2 + (variation(sample.id, 0) % 7000) / 1000.0;
        } else if (!state.selected) {
            state.feet = sample.feet;
            state.walkSeconds = sample.walkSeconds;
            state.stride = 0;
        }
        state.lastSeen = seconds;
        state.selected = true;
        const auto position = sample.feet + glm::vec3(0, 1.5f, 0);
        const bool heard = sample.heardRevision != state.heard && fresh(seconds, sample.heardSeconds);
        const bool chase = sample.chaseRevision != state.chase && fresh(seconds, sample.chaseSeconds);
        state.heard = sample.heardRevision;
        state.chase = sample.chaseRevision;
        const float pitch = .96f + .02f * (sample.id % 5);
        if (chase) {
            cues.push_back({variation(sample.id, state.chase) % 2 ? AudioEffect::chase : AudioEffect::chase2,
                            position, .8f, pitch, sample.id, 3});
        } else if (heard) {
            cues.push_back({variation(sample.id, state.heard) % 2 ? AudioEffect::huh : AudioEffect::intrigued,
                            position, .7f, pitch, sample.id, 2});
        } else if (seconds >= state.nextCall) {
            constexpr std::array<AudioEffect, 3> moans{AudioEffect::zombie, AudioEffect::zombie2, AudioEffect::zombie3};
            cues.push_back({moans[variation(sample.id, ++state.calls) % moans.size()], position,
                            .65f, pitch, sample.id, 1});
        }
        if (chase || heard || seconds >= state.nextCall)
            state.nextCall = seconds + 8 + (variation(sample.id, state.calls + state.heard + state.chase) % 10000) / 1000.0;

        const auto delta = sample.feet - state.feet;
        const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
        const double elapsed = sample.walkSeconds - state.walkSeconds;
        if (sample.walking && horizontal > 1e-5f && horizontal < 5 && elapsed > 0 && elapsed < 1) {
            state.stride += elapsed * (sample.running ? 3.0 : 1.2);
            const double stride = sample.running ? 1.0 : .8;
            if (state.stride >= stride) {
                constexpr std::array<AudioEffect, 4> steps{AudioEffect::footstep, AudioEffect::footstep2,
                                                         AudioEffect::footstep3, AudioEffect::footstep4};
                cues.push_back({steps[variation(sample.id, ++state.steps) % steps.size()], sample.feet,
                                .22f, pitch, std::nullopt, 1});
                state.stride = std::fmod(state.stride, stride); // Never replay a burst after a stall.
            }
        } else if (!sample.walking || horizontal >= 5 || elapsed < 0) {
            state.stride = 0;
        }
        state.feet = sample.feet;
        state.walkSeconds = sample.walkSeconds;
    }
    for (auto it = states_.begin(); it != states_.end();) {
        // Retain a short history through voice preemption so an eighth/ninth
        // nearest swap cannot replay the same still-fresh hearing notification.
        if (!present.count(it->first) && seconds - it->second.lastSeen > 1) it = states_.erase(it);
        else {
            it->second.selected = present.count(it->first) != 0;
            ++it;
        }
    }
    return cues;
}
} // namespace viewer
