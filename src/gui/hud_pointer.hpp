#pragma once

namespace viewer {
enum class HudPointerAction { Move, Press, Release, RightPress, Cancel, Wheel };
struct HudPointerEvent {
    HudPointerAction action;
    float x = 0, y = 0; // Move: window fractions. Wheel: x is signed notches.
};
} // namespace viewer
