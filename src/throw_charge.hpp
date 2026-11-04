#pragma once

#include <algorithm>

namespace viewer {
// Shared right-click timing for grenades and physical object handling.
inline constexpr double throwChargeSeconds = 2;
inline constexpr double throwTapSeconds = .2;
inline float throwChargePower(double heldSeconds) {
    return heldSeconds <= throwTapSeconds ? 0.f :
        static_cast<float>(std::clamp(heldSeconds / throwChargeSeconds, 0.0, 1.0));
}
} // namespace viewer
