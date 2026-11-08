#pragma once
#include "zombie_layer.hpp"
#include <array>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace sword_fixture {
// A stationary anatomical rig: a broad torso is encountered before the right
// shoulder/elbow during a right-to-left cut. Every ragdoll body has a bone, so
// death preserves the same posed joints instead of using a proxy skeleton.
inline viewer::ZombieLayer population(float yawDegrees = 0, float heightOffset = 0) {
    viewer::ZombieLayer layer;
    viewer::AnimatedModel asset;
    const std::array<glm::vec3, 15> origins{{
        {0, .95f, 0}, {0, 1.05f, 0}, {0, 1.62f, 0},
        {.24f, 1.44f, 0}, {.48f, 1.44f, 0}, {-.24f, 1.44f, 0}, {-.48f, 1.44f, 0},
        {.1f, .95f, 0}, {.12f, .55f, 0}, {-.1f, .95f, 0}, {-.12f, .55f, 0},
        {.75f, 1.44f, 0}, {-.75f, 1.44f, 0}, {.14f, .1f, 0}, {-.14f, .1f, 0}
    }};
    const std::array<int, 15> parents{{-1, 0, 1, 1, 3, 1, 5, 0, 7, 0, 9, 4, 6, 8, 10}};
    for (std::size_t bone = 0; bone < origins.size(); ++bone) {
        viewer::SkeletonBone node;
        node.name = bone == 11 || bone == 12 ? "hand" : bone >= 13 ? "foot" : "part_" + std::to_string(bone);
        node.parent = parents[bone];
        node.referenceWorld = glm::translate(glm::mat4(1), origins[bone]);
        node.inverseBind = glm::inverse(node.referenceWorld);
        asset.skeleton.push_back(node);
    }
    for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part) asset.ragdollBones[part] = static_cast<int>(part);
    asset.frameCount = 2; asset.duration = 1;
    for (int frame = 0; frame < 2; ++frame)
        for (const auto& bone : asset.skeleton) asset.boneFrames.push_back(bone.referenceWorld);
    asset.model.boundsMin = {-.8f, 0, -.2f}; asset.model.boundsMax = {.8f, 1.8f, .2f};
    layer.assets.push_back(asset);
    viewer::ZombieBoneCapsule body;
    body.bone = 1; body.a = {0, .12f, 0}; body.b = {0, .40f, 0}; body.radius = .18f;
    layer.capsuleAssets.resize(1); layer.capsuleAssets[0].capsules.push_back(body);
    layer.dismembermentAssets.resize(1);
    for (const auto part : {viewer::RagdollPart::upperArmRight, viewer::RagdollPart::lowerArmRight}) {
        viewer::ZombieJointCollider joint;
        joint.name = part == viewer::RagdollPart::upperArmRight ? "right_shoulder" : "right_elbow";
        joint.bone = static_cast<int>(part); joint.part = part; joint.radius = .045f;
        layer.dismembermentAssets[0].joints.push_back(joint);
        layer.dismembermentAssets[0].meshes.push_back(asset);
    }
    viewer::ZombieInstance instance;
    instance.restRoot = instance.restTransform = instance.transform =
        glm::translate(glm::mat4(1), {0, heightOffset, -1.25f}) *
        glm::rotate(glm::mat4(1), glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    layer.instances.push_back(instance);
    layer.rebuildSpatialIndex();
    return layer;
}

inline void weaken(viewer::ZombieLayer& zombies) {
    for (int hit = 0; hit < 4; ++hit)
        if (!zombies.shoot({0, 1.3f, 0}, {0, 0, -1}, 1, viewer::ShotCaliber::high))
            throw std::runtime_error("Sword fixture could not injure the torso");
}
} // namespace sword_fixture
