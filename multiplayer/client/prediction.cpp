#include "prediction.hpp"
#include <stdexcept>

namespace multiplayer {
Command Prediction::advance(Command command, const viewer::CollisionWorld& collision) {
    if (!ready_) throw std::logic_error("Cannot predict before receiving spawn state");
    if (pending_.size() >= 180) throw std::runtime_error("Server stopped acknowledging commands");
    command.sequence = ++sequence_;
    pending_.push_back(command);
    controller_.update(tickSeconds, command.movement, collision);
    return command;
}
void Prediction::reconcile(const PlayerState& state, const viewer::CollisionWorld& collision) {
    if (state.resolved > sequence_ || state.resolved < resolved_) throw std::runtime_error("Invalid server command acknowledgement");
    resolved_ = state.resolved;
    controller_.restoreMovement(state.movement);
    while (!pending_.empty() && pending_.front().sequence <= resolved_) pending_.pop_front();
    // Replaying only movement produces no audio, shots, or interaction requests.
    for (const auto& command : pending_) controller_.update(tickSeconds, command.movement, collision);
    ready_ = true;
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
