#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace multiplayer {
enum class SoundKind : std::uint8_t { shot, reload, doorLatch, heard, chase };
struct SoundEvent {
    std::uint64_t id = 0, tick = 0;
    SoundKind kind = SoundKind::shot;
    std::uint32_t source = 0; // Player ID for shot/reload/latch; stable zombie ID for reactions.
    std::uint8_t weapon = 0; // Firearm slot for shot/reload.
    glm::vec3 position{0}; // Runtime X east/Y up/Z south metres at event creation.
};
inline constexpr std::size_t maxSoundEvents = 128;
inline constexpr std::uint64_t soundHistoryTicks = 30; // Repeat across snapshots for up to 0.5 s.
inline constexpr float soundRange = 40;
inline bool playerSound(SoundKind kind) {
    return kind == SoundKind::shot || kind == SoundKind::reload || kind == SoundKind::doorLatch;
}
} // namespace multiplayer
