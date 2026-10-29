#include "zombie_animation_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/packing.hpp>

namespace viewer {
namespace {
bool halfRepresentable(float value) {
    // Check before calling GLM: its overflow path deliberately raises a
    // floating-point overflow rather than silently saturating the value.
    return std::isfinite(value) && std::abs(value) <= 65504.0f;
}
}

std::optional<std::vector<PackedZombieAnimationSample>> packZombieAnimation(
    const std::vector<AnimationVertex>& frames) {
    std::vector<PackedZombieAnimationSample> packed;
    packed.reserve(frames.size());
    for (const auto& frame : frames) {
        PackedZombieAnimationSample sample;
        for (int component = 0; component < 4; ++component) {
            if (!halfRepresentable(frame.position[component]) || !halfRepresentable(frame.normal[component]))
                return std::nullopt;
            sample[component] = glm::packHalf1x16(frame.position[component]);
            sample[component + 4] = glm::packHalf1x16(frame.normal[component]);
        }
        const bool nonzeroNormal = frame.normal.x != 0 || frame.normal.y != 0 || frame.normal.z != 0;
        const bool roundedNormalZero = ((sample[4] | sample[5] | sample[6]) & 0x7fffu) == 0;
        if (nonzeroNormal && roundedNormalZero) return std::nullopt;
        packed.push_back(sample);
    }
    return packed;
}

VisibilityBounds zombieAnimationHalfBounds(VisibilityBounds original) {
    auto result = original;
    for (int axis = 0; axis < 3; ++axis) {
        if (halfRepresentable(original.minimum[axis])) {
            const auto rounded = glm::unpackHalf1x16(glm::packHalf1x16(original.minimum[axis]));
            result.minimum[axis] = std::min(original.minimum[axis], rounded);
        }
        if (halfRepresentable(original.maximum[axis])) {
            const auto rounded = glm::unpackHalf1x16(glm::packHalf1x16(original.maximum[axis]));
            result.maximum[axis] = std::max(original.maximum[axis], rounded);
        }
    }
    return result;
}

} // namespace viewer
