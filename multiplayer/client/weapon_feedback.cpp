#include "weapon_feedback.hpp"
#include "weapon_audio.hpp"
#include "weapon_reload.hpp"
#include <algorithm>
#include <cmath>

namespace multiplayer {
void WeaponFeedbackPrediction::reconcile(const PlayerState& state) {
    if (ready_ && state.resolved < state_.resolved) return;
    state_ = state;
    ready_ = true;
    while (!pending_.empty() && pending_.front().sequence <= state.resolved) pending_.pop_front();
    if (reloadSequence_ && state.resolved >= reloadSequence_) reloadSequence_ = 0;
}
WeaponFeedback WeaponFeedbackPrediction::advance(const Command& command, double now,
        const viewer::Weapons& definitions, const viewer::PlayerInventory& inventory) {
    WeaponFeedback result;
    const bool pressed = command.left && !held_;
    held_ = command.left;
    if (!held_) { empty_ = false; needsRelease_ = false; }
    if (!ready_ || !std::isfinite(now) || state_.slot >= viewer::weaponIds.size() || command.slot != state_.slot ||
        state_.vehicle != noVehicle || command.interaction || state_.weaponSwitchRemaining > 0) return result;
    auto weapon = definitions.state(state_.slot);
    int outstanding = 0;
    for (const auto& shot : pending_) if (shot.slot == state_.slot) outstanding += shot.rounds;
    weapon.loaded = static_cast<int>(std::clamp<std::int64_t>(std::int64_t(state_.loaded) - outstanding,
                                                           0, weapon.config.magazineCapacity));
    const bool busy = state_.reloadRemaining > 0 || reloadSequence_ != 0;
    if (command.reload && !busy && !state_.inventoryDragging && !inventory.held() && command.inventoryActions.empty() &&
        planWeaponReload(weapon, inventory)) {
        reloadSequence_ = command.sequence;
        needsRelease_ |= held_;
        if (state_.slot < 4) result.sounds.push_back(reloadSound(state_.slot));
        return result;
    }
    if (busy || needsRelease_ || !(pressed || (held_ && weapon.config.automatic)) || now < readyAt_[state_.slot])
        return result;
    if (weapon.loaded < weapon.config.ammoPerShot) {
        if (!empty_) { result.sounds.push_back(viewer::AudioEffect::emptyClick); empty_ = true; }
        return result;
    }
    pending_.push_back({command.sequence, state_.slot, weapon.config.ammoPerShot});
    readyAt_[state_.slot] = now + weapon.config.shotIntervalSeconds;
    if (state_.slot < 4) {
        result.sounds.push_back(firearmSound(state_.slot));
        result.firearmSlot = state_.slot;
    } else reloadSequence_ = command.sequence; // Silent grenade throw; wait for its confirmed auto-reload.
    return result;
}
} // namespace multiplayer
