#include "zombie_ragdoll_pose.hpp"

#include "zombie_layer.hpp"
#include "zombie_ragdoll.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace viewer {
namespace {

std::size_t index(RagdollPart part) { return static_cast<std::size_t>(part); }

bool descendant(const AnimatedModel& asset, int bone, int ancestor) {
    for (std::size_t depth = 0; bone >= 0 && depth < asset.skeleton.size(); ++depth) {
        if (bone == ancestor) return true;
        bone = asset.skeleton[static_cast<std::size_t>(bone)].parent;
    }
    return false;
}

int limbEnd(const AnimatedModel& asset, int bone, const char* name) {
    int directChild = -1;
    for (std::size_t child = 0; child < asset.skeleton.size(); ++child) {
        if (static_cast<int>(child) == bone || !descendant(asset, static_cast<int>(child), bone)) continue;
        auto lower = asset.skeleton[child].name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.find(name) != std::string::npos) return static_cast<int>(child);
        if (asset.skeleton[child].parent == bone && directChild < 0) directChild = static_cast<int>(child);
    }
    return directChild;
}

} // namespace

bool makeZombieRagdollPoses(const ZombieInstance& instance, const AnimatedModel& asset,
                           RagdollPose& reference, RagdollPose& current) {
    if (instance.activationPose.size() != asset.skeleton.size())
        throw std::invalid_argument("Ragdoll activation pose does not match the skeleton");
    for (const int bone : asset.ragdollBones)
        if (bone < 0 || static_cast<std::size_t>(bone) >= asset.skeleton.size()) return false;

    std::array<int, ragdollPartCount> ends{};
    const auto bone = [&](RagdollPart part) { return asset.ragdollBones[index(part)]; };
    ends[index(RagdollPart::pelvis)] = bone(RagdollPart::torso);
    const int head = bone(RagdollPart::head), torso = bone(RagdollPart::torso);
    const int neck = asset.skeleton[static_cast<std::size_t>(head)].parent;
    ends[index(RagdollPart::torso)] = neck != torso && neck >= 0 && descendant(asset, neck, torso) ? neck : head;
    ends[index(RagdollPart::head)] = -1;
    ends[index(RagdollPart::upperArmLeft)] = bone(RagdollPart::lowerArmLeft);
    ends[index(RagdollPart::upperArmRight)] = bone(RagdollPart::lowerArmRight);
    ends[index(RagdollPart::upperLegLeft)] = bone(RagdollPart::lowerLegLeft);
    ends[index(RagdollPart::upperLegRight)] = bone(RagdollPart::lowerLegRight);
    for (const auto part : {RagdollPart::lowerArmLeft, RagdollPart::lowerArmRight})
        ends[index(part)] = limbEnd(asset, bone(part), "hand");
    for (const auto part : {RagdollPart::lowerLegLeft, RagdollPart::lowerLegRight})
        ends[index(part)] = limbEnd(asset, bone(part), "foot");

    const auto torsoTip = glm::vec3(asset.skeleton[static_cast<std::size_t>(ends[index(RagdollPart::torso)])].referenceWorld[3]);
    const float headLength = 0.36f * glm::length(torsoTip - glm::vec3(asset.skeleton[static_cast<std::size_t>(torso)].referenceWorld[3]));
    for (std::size_t part = 0; part < ragdollPartCount; ++part) {
        const auto b = static_cast<std::size_t>(asset.ragdollBones[part]);
        const auto& rest = asset.skeleton[b].referenceWorld;
        reference.bones[part] = instance.restTransform * rest;
        current.bones[part] = instance.restTransform * instance.activationPose[b];
        if (ends[part] >= 0) {
            const auto end = static_cast<std::size_t>(ends[part]);
            reference.tips[part] = glm::vec3(instance.restTransform * asset.skeleton[end].referenceWorld[3]);
            current.tips[part] = glm::vec3(instance.restTransform * instance.activationPose[end][3]);
        } else {
            const auto origin = glm::vec3(rest[3]);
            glm::vec3 direction(0, headLength, 0);
            if (part != index(RagdollPart::head)) {
                const int parent = asset.skeleton[b].parent;
                if (parent < 0) return false;
                direction = 0.9f * (origin - glm::vec3(asset.skeleton[static_cast<std::size_t>(parent)].referenceWorld[3]));
            }
            const auto tip = glm::vec4(origin + direction, 1);
            reference.tips[part] = glm::vec3(instance.restTransform * tip);
            current.tips[part] = glm::vec3(instance.restTransform * instance.activationPose[b] * glm::inverse(rest) * tip);
        }
    }
    return true;
}

void inheritZombieRagdollMotion(const ZombieInstance& instance, const AnimatedModel& asset,
                               const std::vector<glm::mat4>& previousPose, float seconds,
                               ZombieRagdollWorld& world) {
    if (previousPose.size() != asset.skeleton.size() || instance.activationPose.size() != asset.skeleton.size() ||
        !std::isfinite(seconds) || seconds <= 0)
        throw std::invalid_argument("Ragdoll motion requires matching poses and a positive sample interval");
    const auto bodies = world.bodyTransforms(instance.ragdollHandle);
    for (std::size_t part = 0; part < ragdollPartCount; ++part) {
        const int bone = asset.ragdollBones[part];
        if (bone < 0 || static_cast<std::size_t>(bone) >= asset.skeleton.size()) continue;
        const auto currentBone = instance.restTransform * instance.activationPose[std::size_t(bone)];
        // Track the same point through both poses, including rotation around
        // the anatomical joint rather than just translation of the joint itself.
        const auto center = bodies[part][3];
        const auto localCenter = glm::inverse(currentBone) * center;
        // Subtract in asset space before applying placement: large map
        // translations must not introduce motion into a stationary pose.
        const auto displacement = (instance.activationPose[std::size_t(bone)] - previousPose[std::size_t(bone)]) * localCenter;
        auto velocity = glm::mat3(instance.restTransform) * glm::vec3(displacement) / seconds;
        const float speed = glm::length(velocity);
        if (!std::isfinite(speed)) continue;
        constexpr float maximumSpeed = 20; // m/s; limit launches from looping clip seams.
        if (speed > maximumSpeed) velocity *= maximumSpeed / speed;
        if (speed > 0) world.addLinearVelocity(instance.ragdollHandle, static_cast<RagdollPart>(part), velocity);
    }
}

void updateZombieRagdollSkin(ZombieInstance& instance, const AnimatedModel& asset,
                            const ZombieRagdollWorld& world) {
    const auto count = asset.skeleton.size();
    if (instance.activationPose.size() != count)
        throw std::invalid_argument("Ragdoll activation pose does not match the skeleton");
    instance.ragdollBones.resize(count);
    if (count == 0) return;
    const auto currentBodies = world.bodyTransforms(instance.ragdollHandle);
    const auto initialBodies = world.restBodyTransforms(instance.ragdollHandle);
    const auto inverseInstance = glm::inverse(instance.transform);
    std::vector<glm::mat4> current(count);
    std::vector<bool> ready(count, false);
    for (std::size_t part = 0; part < ragdollPartCount; ++part) {
        const int bone = asset.ragdollBones[part];
        if (bone < 0 || static_cast<std::size_t>(bone) >= count) continue;
        const auto b = static_cast<std::size_t>(bone);
        current[b] = inverseInstance * currentBodies[part] * glm::inverse(initialBodies[part]) *
                     instance.restTransform * instance.activationPose[b];
        ready[b] = true;
    }
    // Parents need not precede their children in glTF node order. Resolve each
    // once, preserving the captured local pose of fingers, feet and other joints.
    std::function<void(std::size_t)> resolve = [&](std::size_t b) {
        if (ready[b]) return;
        const int parent = asset.skeleton[b].parent;
        if (parent >= 0) {
            const auto p = static_cast<std::size_t>(parent);
            resolve(p);
            current[b] = current[p] * glm::inverse(instance.activationPose[p]) * instance.activationPose[b];
        } else current[b] = instance.activationPose[b];
        ready[b] = true;
    };
    for (std::size_t b = 0; b < count; ++b) {
        resolve(b);
        instance.ragdollBones[b] = current[b] * asset.skeleton[b].inverseBind;
    }
}

} // namespace viewer
