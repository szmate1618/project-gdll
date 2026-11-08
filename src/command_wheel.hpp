#pragma once

#include "hud_screen.hpp"
#include <glm/vec2.hpp>
#include <cmath>
#include <cstdint>
#include <optional>

namespace viewer {
enum class SignalKind : std::uint8_t { None, Danger, Move, Backup, Mark };

// The five page choices surround the signal ring. All distances are design dp.
constexpr float commandPageInnerRadius = 160.f, commandPageOuterRadius = 219.f;
inline std::optional<HudScreen> commandWheelPageSelection(glm::vec2 offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        std::hypot(offset.x, offset.y) < commandPageInnerRadius) return {};
    constexpr float sectorAngle = 6.2831853072f / 5;
    const float angle = std::atan2(offset.x, -offset.y);
    const int sector = static_cast<int>(std::floor(angle / sectorAngle + .5f));
    return static_cast<HudScreen>((sector + 5) % 5 + 1);
}

// Offsets are design dp, positive Y down. The center cancels the command.
inline SignalKind signalWheelSelection(glm::vec2 offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        std::hypot(offset.x, offset.y) < 24 || commandWheelPageSelection(offset)) return SignalKind::None;
    const float angle = std::atan2(offset.x, -offset.y);
    const int sector = (static_cast<int>(std::floor(angle / 1.5707963268f + .5f)) + 4) % 4;
    constexpr SignalKind actions[]{SignalKind::Danger, SignalKind::Move, SignalKind::Mark, SignalKind::Backup};
    return actions[sector];
}
} // namespace viewer
