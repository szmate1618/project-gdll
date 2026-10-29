#include "ragdoll_interpolation.hpp"

#include "animated_model.hpp"

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

CorpsePose interpolateRagdollPose(const CorpsePose& before, const CorpsePose& after,
                                const viewer::AnimatedModel& asset, float alpha) {
    if (alpha <= 0) return before;
    if (!(alpha < 1)) return after;
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

} // namespace multiplayer
