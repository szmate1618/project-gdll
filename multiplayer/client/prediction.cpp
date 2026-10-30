#include "prediction.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
Command Prediction::advance(Command command, const viewer::CollisionWorld& collision) {
    if (!ready_) throw std::logic_error("Cannot predict before receiving spawn state");
    if (pending_.size() >= 180) throw std::runtime_error("Connection lost: server stopped responding (command acknowledgement timeout)");
    command.sequence = ++sequence_;
    pending_.push_back(command);
    previousEye_ = eye();
    if (!seated_) controller_.update(tickSeconds, command.movement, collision);
    return command;
}
void Prediction::reconcile(const PlayerState& state, const viewer::CollisionWorld& collision) {
    if (state.resolved > sequence_ || state.resolved < resolved_) throw std::runtime_error("Invalid server command acknowledgement");
    const auto before = eye();
    const bool walking = ready_ && !seated_ && state.vehicle == noVehicle;
    resolved_ = state.resolved;
    controller_.restoreMovement(state.movement);
    seated_ = state.vehicle != noVehicle;
    while (!pending_.empty() && pending_.front().sequence <= resolved_) pending_.pop_front();
    // Replaying only movement produces no audio, shots, or interaction requests.
    if (!seated_) for (const auto& command : pending_) controller_.update(tickSeconds, command.movement, collision);
    const auto correction = eye() - before;
    // Translate both endpoints together so the session's visual correction
    // cancels the same error at every alpha. Replays are not new render steps.
    if (walking && glm::length(correction) < 2) previousEye_ += correction;
    else previousEye_ = eye(); // Spawn, teleport, or change of vehicle seat.
    ready_ = true;
}
glm::vec3 Prediction::presentedEye(float alpha) const {
    return glm::mix(previousEye_, eye(), std::clamp(alpha, 0.f, 1.f));
}
Commands Prediction::commandsToSend() const {
    Commands result;
    for (const auto& c : pending_) {
        if (result.commands.size() == 31) break;
        result.commands.push_back(c);
    }
    if (pending_.size() > 31) result.commands.push_back(pending_.back());
    return result;
}
} // namespace multiplayer
