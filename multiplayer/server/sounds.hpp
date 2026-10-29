#pragma once
#include "sound_events.hpp"
#include <deque>
#include <vector>

namespace multiplayer {
// Short repeated event history; contains no ambient moans or movement sounds.
class ServerSounds {
public:
    void emit(std::uint64_t tick, SoundKind kind, std::uint32_t source, glm::vec3 position, std::uint8_t weapon = 0);
    void expire(std::uint64_t tick);
    std::vector<SoundEvent> forListener(std::uint64_t tick, std::uint32_t player, glm::vec3 eye) const;
private:
    std::uint64_t nextId_ = 1;
    std::deque<SoundEvent> events_;
};
} // namespace multiplayer
