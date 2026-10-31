#include "ragdoll_pose_codec.hpp"
#include "animated_model.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <glm/gtc/quaternion.hpp>

namespace multiplayer {
namespace {
constexpr double tau = 6.28318530717958647692;

bool invertibleAffine(const glm::dmat4& matrix) {
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (!std::isfinite(matrix[c][r]) || std::abs(matrix[c][r]) > 1e8) return false;
    return std::abs(matrix[0][3]) < 1e-5 && std::abs(matrix[1][3]) < 1e-5 &&
        std::abs(matrix[2][3]) < 1e-5 && std::abs(matrix[3][3] - 1) < 1e-5 &&
        glm::determinant(glm::dmat3(matrix)) != 0;
}

bool rotationBasis(const glm::dmat3& basis) {
    const auto identity = glm::transpose(basis) * basis;
    if (!std::isfinite(glm::determinant(basis)) || std::abs(glm::determinant(basis) - 1) > .001) return false;
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r)
        if (!std::isfinite(identity[c][r]) || std::abs(identity[c][r] - (c == r ? 1 : 0)) > .001) return false;
    return true;
}

struct Rig {
    std::vector<glm::dmat4> local;
    std::vector<glm::dmat3> parentBasis;
    std::vector<std::size_t> order;
};

std::optional<Rig> prepare(const viewer::AnimatedModel& asset) {
    const auto count = asset.skeleton.size();
    if (!count || count > maxBones) return {};
    Rig rig;
    rig.local.resize(count); rig.parentBasis.resize(count);
    std::vector<std::uint8_t> visited(count);
    std::function<bool(std::size_t)> visit = [&](std::size_t b) {
        if (visited[b]) return visited[b] == 2;
        visited[b] = 1;
        const auto& bone = asset.skeleton[b];
        if (bone.parent < -1 || bone.parent >= static_cast<int>(count) ||
            !invertibleAffine(glm::dmat4(bone.referenceWorld)) ||
            !invertibleAffine(glm::dmat4(bone.inverseBind))) return false;
        glm::dmat4 parent(1);
        if (bone.parent >= 0) {
            const auto p = static_cast<std::size_t>(bone.parent);
            if (!visit(p)) return false;
            parent = glm::dmat4(asset.skeleton[p].referenceWorld);
        }
        rig.local[b] = glm::inverse(parent) * glm::dmat4(bone.referenceWorld);
        rig.parentBasis[b] = glm::dmat3(parent);
        visited[b] = 2;
        rig.order.push_back(b);
        return true;
    };
    for (std::size_t b = 0; b < count; ++b) if (!visit(b)) return {};
    return rig;
}

std::array<std::uint8_t, 3> packRotation(const glm::dmat3& basis) {
    const auto angles = glm::eulerAngles(glm::normalize(glm::quat_cast(basis)));
    std::array<std::uint8_t, 3> packed{};
    for (int axis = 0; axis < 3; ++axis) {
        const auto step = std::llround(angles[axis] * 256 / tau);
        packed[axis] = static_cast<std::uint8_t>((step % 256 + 256) % 256);
    }
    return packed;
}

glm::dmat3 unpackRotation(const std::array<std::uint8_t, 3>& packed) {
    glm::dvec3 angles;
    for (int axis = 0; axis < 3; ++axis) angles[axis] = packed[axis] * tau / 256;
    return glm::mat3_cast(glm::normalize(glm::dquat(angles)));
}
} // namespace

std::optional<CompactRagdollPose> compactRagdollPose(const viewer::AnimatedModel& asset,
                                                  const CorpsePose& pose) {
    const auto rig = prepare(asset);
    if (!rig || pose.bones.size() != asset.skeleton.size()) return {};
    CompactRagdollPose packed;
    packed.rotations.resize(pose.bones.size());
    std::vector<glm::dmat4> world(pose.bones.size());
    for (std::size_t b = 0; b < world.size(); ++b) {
        world[b] = glm::dmat4(pose.bones[b]) * glm::inverse(glm::dmat4(asset.skeleton[b].inverseBind));
        if (!invertibleAffine(world[b])) return {};
    }
    for (std::size_t b = 0; b < world.size(); ++b) {
        const int parent = asset.skeleton[b].parent;
        const auto local = parent < 0 ? world[b] : glm::inverse(world[static_cast<std::size_t>(parent)]) * world[b];
        const auto turn = glm::dmat3(local) * glm::inverse(glm::dmat3(rig->local[b]));
        if (!rotationBasis(turn)) return {};
        packed.rotations[b] = packRotation(turn);
        // Imported rigs can use centimetres inside their parent frame. Convert
        // offsets to reference asset metres before quantizing, once per joint.
        const auto delta = rig->parentBasis[b] * glm::dvec3(local[3] - rig->local[b][3]);
        RagdollJointOffset offset;
        offset.bone = static_cast<std::uint8_t>(b);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(delta[axis]) || std::abs(delta[axis]) > 8388) return {};
            offset.millimetres[axis] = static_cast<std::int32_t>(std::llround(delta[axis] * 1000));
        }
        if (offset.millimetres != std::array<std::int32_t, 3>{}) packed.offsets.push_back(offset);
    }
    // A nonuniform/sheared parent can amplify an otherwise rigid local-angle
    // rounding into a change of the child's world basis. Keep those poses raw.
    std::vector<glm::dmat3> decoded(world.size());
    for (const auto b : rig->order) {
        auto basis = unpackRotation(packed.rotations[b]) * glm::dmat3(rig->local[b]);
        const int parent = asset.skeleton[b].parent;
        decoded[b] = parent < 0 ? basis : decoded[static_cast<std::size_t>(parent)] * basis;
        if (!rotationBasis(decoded[b] * glm::inverse(glm::dmat3(world[b])))) return {};
    }
    return packed;
}

void expandRagdollPose(const viewer::AnimatedModel& asset, CorpsePose& pose) {
    if (!pose.compact) return;
    const auto rig = prepare(asset);
    const auto& packed = *pose.compact;
    if (!rig || packed.rotations.size() != asset.skeleton.size())
        throw std::runtime_error("Compact corpse pose does not match its skeleton");
    std::vector<glm::dvec3> offsets(packed.rotations.size(), glm::dvec3(0));
    int previous = -1;
    for (const auto& offset : packed.offsets) {
        if (offset.bone >= offsets.size() || offset.bone <= previous)
            throw std::runtime_error("Invalid compact corpse joint offset");
        previous = offset.bone;
        offsets[offset.bone] = glm::dvec3(offset.millimetres[0], offset.millimetres[1], offset.millimetres[2]) / 1000.;
    }
    std::vector<glm::dmat4> world(packed.rotations.size());
    pose.bones.resize(world.size());
    for (const auto b : rig->order) {
        auto local = glm::dmat4(unpackRotation(packed.rotations[b]) * glm::dmat3(rig->local[b]));
        local[3] = rig->local[b][3] + glm::dvec4(glm::inverse(rig->parentBasis[b]) * offsets[b], 0);
        const int parent = asset.skeleton[b].parent;
        world[b] = parent < 0 ? local : world[static_cast<std::size_t>(parent)] * local;
        pose.bones[b] = glm::mat4(world[b] * glm::dmat4(asset.skeleton[b].inverseBind));
    }
    pose.compact.reset();
}
} // namespace multiplayer
