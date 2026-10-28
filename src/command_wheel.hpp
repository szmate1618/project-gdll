#pragma once

#include <glm/vec2.hpp>
#include <cmath>
#include <cstdint>

namespace viewer {
enum class SignalKind : std::uint8_t { None, Danger, Move, Backup };

// Offsets are design dp, positive Y down. The center cancels the command.
inline SignalKind signalWheelSelection(glm::vec2 offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        offset.x * offset.x + offset.y * offset.y < 24 * 24) return SignalKind::None;
    const float angle = std::atan2(offset.x, -offset.y);
    if (std::abs(angle) < 1.0471975512f) return SignalKind::Danger;
    return angle > 0 ? SignalKind::Move : SignalKind::Backup;
}
} // namespace viewer
