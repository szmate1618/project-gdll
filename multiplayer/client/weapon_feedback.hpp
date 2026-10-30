#pragma once

#include "protocol.hpp"
#include "audio.hpp"
#include "weapons.hpp"
#include <array>
#include <deque>
#include <optional>
#include <vector>

namespace multiplayer {
struct WeaponFeedback {
    std::vector<viewer::AudioEffect> sounds;
    std::optional<std::uint8_t> firearmSlot; // One successful firearm shot, regardless of pellet count.
};

// Presentation prediction only; never changes authoritative ammo, damage or
// hit processing. One decision drives local sound, recoil and muzzle flash even
// when audio playback is disabled. Reconciliation never replays feedback.
class WeaponFeedbackPrediction {
public:
    void reconcile(const PlayerState& state);
    WeaponFeedback advance(const Command& command, double now,
        const viewer::Weapons& definitions, const viewer::PlayerInventory& inventory);
private:
    struct Shot { std::uint64_t sequence; std::uint8_t slot; int rounds; };
    PlayerState state_;
    std::deque<Shot> pending_;
    std::array<double, viewer::weaponIds.size()> readyAt_{};
    std::uint64_t reloadSequence_ = 0;
    bool ready_ = false, held_ = false, empty_ = false, needsRelease_ = false;
};
} // namespace multiplayer
