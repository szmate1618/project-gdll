#pragma once

#include "animated_model.hpp"
#include "visibility.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace viewer {

// Two RGBA16F texture-buffer texels: position xyzw, then normal xyzw. Positions
// remain in the baked asset's scene frame; instance/world transforms stay FP32.
using PackedZombieAnimationSample = std::array<std::uint16_t, 8>;
static_assert(sizeof(PackedZombieAnimationSample) == 16, "Packed animation samples require two RGBA16F texels");

// Transient GPU-upload storage; canonical CPU animation samples are unchanged.
// nullopt retains FP32 for a nonfinite/out-of-range component or a nonzero
// normal whose xyz components would all round to zero. No values are clipped.
std::optional<std::vector<PackedZombieAnimationSample>> packZombieAnimation(
    const std::vector<AnimationVertex>& frames);

// Enclose both original and decoded-half positions using only six endpoint
// conversions. Half rounding is monotonic, so rounded extrema also enclose
// every decoded sample and linear interpolation between samples. Endpoints
// outside finite half range stay unchanged; their geometry uses FP32 fallback.
VisibilityBounds zombieAnimationHalfBounds(VisibilityBounds original);

} // namespace viewer
