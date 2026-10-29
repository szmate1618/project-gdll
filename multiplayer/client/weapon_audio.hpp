#pragma once
#include "protocol.hpp"
#include "audio.hpp"
#include "weapons.hpp"
#include <array>
#include <deque>

namespace multiplayer {
// Presentation prediction only: never changes authoritative ammo, recoil or hits.
// Reconciliation retires acknowledged predictions without replaying their sounds.
class WeaponAudioPrediction {
public:
    void reconcile(const PlayerState& state);
    std::vector<viewer::AudioEffect> advance(const Command& command, double now,
        const viewer::Weapons& definitions, const viewer::PlayerInventory& inventory);
private:
    struct Shot { std::uint64_t sequence; std::uint8_t slot; int rounds; };
    PlayerState state_;
    std::deque<Shot> pending_;
    std::array<double, viewer::weaponIds.size()> readyAt_{};
    std::uint64_t reloadSequence_ = 0;
    bool ready_ = false, held_ = false, empty_ = false, needsRelease_ = false;
};
viewer::AudioEffect firearmSound(std::size_t slot);
viewer::AudioEffect reloadSound(std::size_t slot);
} // namespace multiplayer
