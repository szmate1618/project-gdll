#pragma once

#include <cstddef>
#include <cstdint>

namespace viewer {

// The small set of rigid bodies used by the approximate Box3D ragdoll.
// Asset skeletons map their bones to these parts by name when possible.
enum class RagdollPart : std::uint8_t {
    pelvis,
    torso,
    head,
    upperArmLeft,
    lowerArmLeft,
    upperArmRight,
    lowerArmRight,
    upperLegLeft,
    lowerLegLeft,
    upperLegRight,
    lowerLegRight,
    count
};

constexpr std::size_t ragdollPartCount = static_cast<std::size_t>(RagdollPart::count);

} // namespace viewer
