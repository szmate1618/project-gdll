#include "sound_cues.hpp"
#include "weapon_audio.hpp"
#include <algorithm>

namespace multiplayer {
std::vector<viewer::AudioCue> RemoteSoundCues::receive(const Snapshot& snapshot, std::uint32_t self,
        std::vector<SoundEvent>* shots, double playbackSeconds) {
    if (shots) shots->clear();
    std::vector<viewer::AudioCue> cues;
    if (!initialized_) {
        initialized_ = true;
        joinedTick_ = latestTick_ = snapshot.tick;
        for (const auto& explosion : snapshot.explosions) lastExplosion_ = std::max(lastExplosion_, explosion.id);
        return cues;
    }
    if (snapshot.tick < latestTick_) return cues;
    latestTick_ = snapshot.tick;
    for (auto it = seen_.begin(); it != seen_.end();) {
        if (snapshot.tick > it->second && snapshot.tick - it->second > soundHistoryTicks) it = seen_.erase(it);
        else ++it;
    }
    for (const auto& event : snapshot.sounds) {
        if (event.tick <= joinedTick_ || event.tick > snapshot.tick || snapshot.tick - event.tick > 18 ||
            (playerSound(event.kind) && event.source == self)) continue;
        // Revisit the latest snapshot each frame. A not-yet-rendered shot must
        // remain unseen until both its sound and visible weapon can fire.
        if (event.kind == SoundKind::shot && double(event.tick) > playbackSeconds * 60) continue;
        if (!seen_.emplace(event.id, event.tick).second) continue;
        viewer::AudioCue cue{viewer::AudioEffect::doorLatch, event.position, .6f, 1, std::nullopt, 1};
        switch (event.kind) {
        case SoundKind::shot:
            cue.effect = firearmSound(event.weapon); cue.gain = .8f;
            if (shots) shots->push_back(event);
            break;
        case SoundKind::reload: cue.effect = reloadSound(event.weapon); cue.gain = .5f; break;
        case SoundKind::doorLatch: break;
        case SoundKind::heard:
        case SoundKind::chase:
            cue.zombie = event.source;
            cue.priority = event.kind == SoundKind::chase ? 3 : 2;
            cue.effect = event.kind == SoundKind::chase ? (event.id % 2 ? viewer::AudioEffect::chase : viewer::AudioEffect::chase2) :
                (event.id % 2 ? viewer::AudioEffect::huh : viewer::AudioEffect::intrigued);
            cue.gain = event.kind == SoundKind::chase ? .8f : .7f;
            cue.pitch = .96f + .02f * (event.source % 5);
            break;
        }
        cues.push_back(cue);
    }
    for (const auto& explosion : snapshot.explosions) {
        if (explosion.id <= lastExplosion_) continue;
        if (explosion.age <= .3f) cues.push_back({viewer::AudioEffect::explosion, explosion.position, 1, 1, std::nullopt, 1});
    }
    for (const auto& explosion : snapshot.explosions) lastExplosion_ = std::max(lastExplosion_, explosion.id);
    return cues;
}
} // namespace multiplayer
