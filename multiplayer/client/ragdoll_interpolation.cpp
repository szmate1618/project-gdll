#include "ragdoll_interpolation.hpp"

#include "animated_model.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace multiplayer {
namespace {

bool affine(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    constexpr double tolerance = 1e-4;
    return std::abs(matrix[0][3]) < tolerance && std::abs(matrix[1][3]) < tolerance &&
        std::abs(matrix[2][3]) < tolerance && std::abs(matrix[3][3] - 1) < tolerance;
}

bool interpolateAffine(const glm::dmat4& before, const glm::dmat4& after,
                       double alpha, glm::dmat4& result) {
    if (!affine(before) || !affine(after)) return false;
    const glm::dmat3 basis(before);
    const double determinant = glm::determinant(basis);
    if (!std::isfinite(determinant) || determinant == 0) return false;

    // Physics supplies rigid world motion on the left of a fixed affine map.
    // Taking the relative basis cancels that map instead of decomposing away
    // the model's reflection, scale or shear. Double precision keeps this
    // cancellation stable for small imported asset scales.
    const auto rotation = glm::dmat3(after) * glm::inverse(basis);
    const auto orthogonal = glm::transpose(rotation) * rotation;
    constexpr double tolerance = .005;
    if (std::abs(glm::determinant(rotation) - 1) > tolerance) return false;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            if (!std::isfinite(orthogonal[column][row]) ||
                std::abs(orthogonal[column][row] - (column == row ? 1 : 0)) > tolerance)
                return false;
    const auto turn = glm::normalize(glm::quat_cast(rotation));
    const auto interpolated = glm::mat3_cast(glm::slerp(glm::dquat(1, 0, 0, 0), turn, alpha)) * basis;
    result = glm::dmat4(interpolated);
    result[3] = glm::dvec4(glm::mix(glm::dvec3(before[3]), glm::dvec3(after[3]), alpha), 1);
    return true;
}

} // namespace

static CorpsePose blendRagdollPose(const CorpsePose& before, const CorpsePose& after,
                                 const viewer::AnimatedModel& asset, float alpha) {
    if (before.id != after.id || before.asset != after.asset || before.severedJoint != after.severedJoint ||
        before.bones.size() != after.bones.size() || before.bones.size() != asset.skeleton.size()) return after;

    const glm::dmat4 oldTransform(before.transform), newTransform(after.transform);
    glm::dmat4 transform, root;
    if (!interpolateAffine(oldTransform, newTransform, alpha, transform) ||
        !interpolateAffine(glm::dmat4(before.root), glm::dmat4(after.root), alpha, root)) return after;

    CorpsePose result = after;
    result.transform = glm::mat4(transform);
    result.root = glm::mat4(root);
    const auto inverseTransform = glm::inverse(transform);
    for (std::size_t bone = 0; bone < before.bones.size(); ++bone) {
        const glm::dmat4 inverseBind(asset.skeleton[bone].inverseBind);
        if (!affine(inverseBind) || glm::determinant(glm::dmat3(inverseBind)) == 0) return after;
        const auto bind = glm::inverse(inverseBind);
        glm::dmat4 world;
        // Undo skinning's inverse bind before blending: the bone's anatomical
        // joint is its rotation pivot, not the original mesh's origin. World
        // joint frames also preserve independently moving detached limbs.
        if (!interpolateAffine(oldTransform * glm::dmat4(before.bones[bone]) * bind,
                newTransform * glm::dmat4(after.bones[bone]) * bind, alpha, world)) return after;
        result.bones[bone] = glm::mat4(inverseTransform * world * inverseBind);
    }
    return result;
}

CorpsePose interpolateRagdollPose(const CorpsePose& before, const CorpsePose& after,
                                const viewer::AnimatedModel& asset, float alpha) {
    if (alpha <= 0) return before;
    if (!(alpha < 1)) return after;
    return blendRagdollPose(before, after, asset, alpha);
}

CorpsePose predictRagdollPose(const CorpsePose& before, const CorpsePose& after,
                            const viewer::AnimatedModel& asset, double seconds) {
    if (after.sleeping || after.tick <= before.tick || !std::isfinite(seconds) || seconds <= 0 ||
        before.bones.size() != after.bones.size() || after.bones.size() != asset.skeleton.size()) return after;
    const double interval = double(after.tick - before.tick) / 60;
    if (interval > .6 || before.sleeping) return after; // Wake/re-entry has no measured velocity yet.
    // Reject teleports and extreme joint motion rather than projecting them.
    // The shortest rotation arc is bounded by the two samples; prediction never
    // advances by more than two ordinary five-Hz sample intervals.
    constexpr double maximumSpeed = 20; // World meters/second.
    if (glm::length(glm::dvec3(after.transform[3] - before.transform[3])) > maximumSpeed * interval) return after;
    for (std::size_t bone = 0; bone < after.bones.size(); ++bone) {
        const auto bind = glm::inverse(glm::dmat4(asset.skeleton[bone].inverseBind));
        const auto a = glm::dmat4(before.transform) * glm::dmat4(before.bones[bone]) * bind;
        const auto b = glm::dmat4(after.transform) * glm::dmat4(after.bones[bone]) * bind;
        if (glm::length(glm::dvec3(b[3] - a[3])) > maximumSpeed * interval) return after;
    }
    return blendRagdollPose(before, after, asset, float(1 + std::min(seconds, .4) / interval));
}

} // namespace multiplayer
