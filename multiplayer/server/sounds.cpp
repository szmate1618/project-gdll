#include "sounds.hpp"

namespace multiplayer {
void ServerSounds::expire(std::uint64_t tick) {
    while (!events_.empty() && tick - events_.front().tick > soundHistoryTicks) events_.pop_front();
}
void ServerSounds::emit(std::uint64_t tick, SoundKind kind, std::uint32_t source, glm::vec3 position, std::uint8_t weapon) {
    expire(tick);
    if (events_.size() == maxSoundEvents) events_.pop_front();
    events_.push_back({nextId_++, tick, kind, source, weapon, position});
}
std::vector<SoundEvent> ServerSounds::forListener(std::uint64_t tick, std::uint32_t player, glm::vec3 eye) const {
    std::vector<SoundEvent> result;
    for (const auto& event : events_) {
        const auto delta = event.position - eye;
        if (event.tick <= tick && tick - event.tick <= soundHistoryTicks &&
            !(playerSound(event.kind) && event.source == player) && glm::dot(delta, delta) <= soundRange * soundRange)
            result.push_back(event);
    }
    return result;
}
} // namespace multiplayer
