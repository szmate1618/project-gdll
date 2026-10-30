#pragma once
#include "protocol.hpp"
#include "collision_world.hpp"
#include <deque>

namespace multiplayer {
class Prediction {
public:
    Command advance(Command command, const viewer::CollisionWorld& collision);
    void reconcile(const PlayerState& authoritative, const viewer::CollisionWorld& collision);
    Commands commandsToSend() const;
    bool ready() const { return ready_; }
    // Reserve presentation metadata before advance stores this numbered input.
    std::uint64_t nextSequence() const { return sequence_ + 1; }
    glm::vec3 eye() const { return controller_.eyePosition(); }
    const viewer::FPSController& controller() const { return controller_; }
    std::size_t outstanding() const { return pending_.size(); }
private:
    viewer::FPSController controller_;
    std::deque<Command> pending_;
    std::uint64_t sequence_ = 0, resolved_ = 0;
    bool ready_ = false;
    bool seated_ = false;
};
} // namespace multiplayer
