#include "ragdoll_pose_codec.hpp"
#include "client/ragdoll_interpolation.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace multiplayer;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

viewer::AnimatedModel rig() {
    viewer::AnimatedModel asset;
    asset.skeleton.resize(3);
    // Imported centimetre rig, deliberately not in parent-before-child order.
    asset.skeleton[1].referenceWorld = glm::scale(glm::mat4(1), glm::vec3(.01f));
    asset.skeleton[2].parent = 1;
    asset.skeleton[2].referenceWorld = glm::translate(asset.skeleton[1].referenceWorld, {0, 100, 0}) *
        glm::rotate(glm::mat4(1), glm::radians(20.f), {0, 0, 1});
    asset.skeleton[0].parent = 2;
    asset.skeleton[0].referenceWorld = glm::translate(asset.skeleton[2].referenceWorld, {50, 0, 0});
    for (auto& bone : asset.skeleton) bone.inverseBind = glm::inverse(bone.referenceWorld);
    return asset;
}

CorpsePose posed(const viewer::AnimatedModel& asset) {
    CorpsePose pose;
    pose.id = 7; pose.asset = 2; pose.version = 123; pose.tick = 120; pose.severedJoint = 4;
    pose.transform = glm::translate(glm::mat4(1), {123, 4, -567});
    pose.root = glm::rotate(pose.transform, .3f, {0, 1, 0});
    std::vector<glm::mat4> world(3);
    world[1] = glm::rotate(glm::mat4(1), glm::radians(40.f), {0, 0, 1}) * asset.skeleton[1].referenceWorld;
    auto middle = glm::inverse(asset.skeleton[1].referenceWorld) * asset.skeleton[2].referenceWorld;
    const auto middlePosition = middle[3] + glm::vec4(1.2f, 0, 0, 0);
    middle = glm::rotate(glm::mat4(1), glm::radians(38.f), {0, 1, 0}) * middle;
    middle[3] = middlePosition;
    world[2] = world[1] * middle;
    auto end = glm::inverse(asset.skeleton[2].referenceWorld) * asset.skeleton[0].referenceWorld;
    const auto endPosition = end[3] + glm::vec4(300, 0, 0, 0); // Detached assembly, three metres away.
    end = glm::rotate(glm::mat4(1), glm::radians(-75.f), {1, 0, 0}) * end;
    end[3] = endPosition;
    world[0] = world[2] * end;
    for (std::size_t b = 0; b < world.size(); ++b) pose.bones.push_back(world[b] * asset.skeleton[b].inverseBind);
    return pose;
}

CorpsePose roundtrip(const viewer::AnimatedModel& asset, CorpsePose pose) {
    pose.compact = compactRagdollPose(asset, pose);
    require(pose.compact.has_value(), "Representable ragdoll did not use compact encoding");
    const auto bytes = encode(pose);
    auto copy = std::get<CorpsePose>(decode(bytes.data(), bytes.size()));
    require(copy.compact.has_value() && copy.bones.empty(), "Wire decoding must defer reconstruction until the rig is available");
    expandRagdollPose(asset, copy);
    require(!copy.compact && copy.bones.size() == asset.skeleton.size(), "Compact pose was not expanded before playback");
    return copy;
}

void hierarchyAndDetachedOffsets() {
    const auto asset = rig();
    const auto pose = posed(asset);
    const auto packed = compactRagdollPose(asset, pose);
    require(packed && packed->rotations.size() == 3 && packed->offsets.size() == 2,
        "Static joint offsets should be shared; moving joint offsets must remain sparse");
    const auto copy = roundtrip(asset, pose);
    require(copy.id == pose.id && copy.version == pose.version && copy.tick == pose.tick &&
            copy.severedJoint == pose.severedJoint && copy.transform == pose.transform && copy.root == pose.root,
            "Compact encoding changed coherent roots or corpse identity");
    for (std::size_t b = 0; b < pose.bones.size(); ++b) {
        const auto bind = glm::inverse(asset.skeleton[b].inverseBind);
        const auto before = pose.bones[b] * bind, after = copy.bones[b] * bind;
        require(glm::distance(glm::vec3(before[3]), glm::vec3(after[3])) < .1f,
            "Joint reconstruction misplaced a centimetre rig or detached assembly");
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r)
            require(std::abs(glm::dot(glm::vec3(before[c]), glm::vec3(before[r])) -
                             glm::dot(glm::vec3(after[c]), glm::vec3(after[r]))) < 1e-7f,
                    "Quantizing rotations changed fixed bone scale");
    }
}

void rotationRangeAndAffineShape() {
    viewer::AnimatedModel asset;
    auto& bone = asset.skeleton.emplace_back();
    bone.referenceWorld = glm::scale(glm::mat4(1), {-2, 3, 4});
    bone.referenceWorld[1].x = .7f;
    bone.inverseBind = glm::inverse(bone.referenceWorld);
    for (int i = 0; i < 80; ++i) {
        const glm::vec3 angles = glm::radians(glm::vec3(-180 + i * 4.91f, -90 + i * 2.31f, 180 - i * 5.13f));
        const auto rotation = glm::mat4_cast(glm::quat(angles));
        CorpsePose pose; pose.version = 1;
        pose.bones = {rotation * bone.referenceWorld * bone.inverseBind};
        const auto copy = roundtrip(asset, pose);
        const auto expected = rotation * bone.referenceWorld;
        const auto actual = copy.bones[0] * glm::inverse(bone.inverseBind);
        const auto turn = glm::normalize(glm::quat_cast(glm::mat3(actual) * glm::inverse(glm::mat3(bone.referenceWorld))));
        const auto error = 2 * std::acos(std::clamp(std::abs(glm::dot(turn, glm::normalize(glm::quat(angles)))), 0.f, 1.f));
        require(error < glm::radians(2.2f), "XYZ rotation quantization exceeded its angular error bound");
        require(glm::determinant(glm::mat3(actual)) < 0, "Compact encoding lost a reflected basis");
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r)
            require(std::abs(glm::dot(glm::vec3(expected[c]), glm::vec3(expected[r])) -
                             glm::dot(glm::vec3(actual[c]), glm::vec3(actual[r]))) < .0001f,
                    "Compact encoding lost fixed nonuniform scale or shear");
    }
}

void malformedAndFallback() {
    auto asset = rig();
    auto pose = posed(asset);
    pose.bones[0][0] *= 1.1f;
    require(!compactRagdollPose(asset, pose), "Changing scale must retain the raw matrix encoding");
    const auto raw = encode(pose);
    require(std::get<CorpsePose>(decode(raw.data(), raw.size())).bones == pose.bones,
        "Raw fallback lost its exact matrices");
    auto stretched = rig();
    for (auto& bone : stretched.skeleton) {
        bone.referenceWorld = glm::scale(glm::mat4(1), {1, 3, 7}) * bone.referenceWorld;
        bone.inverseBind = glm::inverse(bone.referenceWorld);
    }
    require(!compactRagdollPose(stretched, posed(stretched)),
        "Angle rounding under a stretched parent must not deform the child's fixed world basis");
    pose = posed(asset); pose.compact = compactRagdollPose(asset, pose);
    const auto bytes = encode(pose);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        bool rejected = false;
        try { decode(bytes.data(), size); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Truncated compact corpse was accepted");
    }
    pose.compact->offsets.push_back(pose.compact->offsets.back());
    bool rejected = false;
    try { encode(pose); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Duplicate compact joint offsets were accepted");
    asset.skeleton[1].parent = 0;
    require(!compactRagdollPose(asset, posed(rig())), "Cyclic skeleton was accepted by the compact codec");
}

void authoredAssets(const std::filesystem::path& directory) {
    std::size_t bytesBefore = 0, bytesAfter = 0, samples = 0, offsets = 0;
    double maximumJointError = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".glb") continue;
        auto layer = viewer::loadZombieLayer(entry.path(), {glm::mat4(1), glm::translate(glm::mat4(1), {4, 0, 0})});
        require(layer.knockDown(0, {3, 5, 2}, .5), "Authored ragdoll activation failed");
        const auto& joints = layer.dismembermentAssets[0].joints;
        const auto head = std::find_if(joints.begin(), joints.end(), [](const auto& joint) {
            return joint.part == viewer::RagdollPart::head;
        });
        require(head != joints.end(), "Authored fixture lacks its detached-head variant");
        layer.instances[1].phase = 0;
        const auto animation = viewer::sampleAnimatedPose(layer.assets[0], .5);
        const auto center = glm::vec3(layer.instances[1].transform * viewer::zombieJointTransform(*head, animation) * glm::vec4(0, 0, 0, 1));
        require(layer.shoot(center + glm::vec3(0, 0, .4f), {0, 0, -1}, .5) &&
                layer.instances[1].severedJoint == int(head - joints.begin()),
            "Authored detached-head activation failed");
        for (int frame = 0; frame < 60; ++frame) {
            layer.update(1.f / 60);
            if (frame % 6) continue;
            for (const auto& zombie : layer.instances) {
                const auto& asset = layer.assets[0];
                CorpsePose pose{0, 0, zombie.ragdollPoseVersion, zombie.transform, zombie.restRoot,
                    zombie.ragdollBones, static_cast<std::int16_t>(zombie.severedJoint),
                    static_cast<std::uint64_t>(frame + 1), zombie.ragdollSleeping, {}};
                const auto rawBytes = encode(pose);
                pose.compact = compactRagdollPose(asset, pose);
                require(pose.compact.has_value(), "Shipped rig failed compact encoding during physics motion");
                offsets += pose.compact->offsets.size();
                const auto packedBytes = encode(pose);
                auto received = std::get<CorpsePose>(decode(packedBytes.data(), packedBytes.size()));
                expandRagdollPose(asset, received);
                for (std::size_t b = 0; b < asset.skeleton.size(); ++b) {
                    const auto bind = glm::inverse(asset.skeleton[b].inverseBind);
                    const auto before = glm::vec3(pose.transform * pose.bones[b] * bind * glm::vec4(0, 0, 0, 1));
                    const auto after = glm::vec3(received.transform * received.bones[b] * bind * glm::vec4(0, 0, 0, 1));
                    maximumJointError = std::max(maximumJointError, double(glm::distance(before, after)));
                }
                bytesBefore += rawBytes.size(); bytesAfter += packedBytes.size(); ++samples;
            }
        }
    }
    require(samples > 0 && bytesAfter < bytesBefore / 5, "Authored corpse compression failed to reduce pose traffic");
    std::cout << "Pose samples=" << samples << " raw_bytes=" << bytesBefore << " compact_bytes=" << bytesAfter
              << " saving_percent=" << 100. * (1 - double(bytesAfter) / bytesBefore)
              << " offsets_per_pose=" << double(offsets) / samples << " maximum_joint_error_m=" << maximumJointError << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        hierarchyAndDetachedOffsets(); rotationRangeAndAffineShape(); malformedAndFallback();
        if (argc > 1) authoredAssets(argv[1]);
        std::cout << "Compact ragdoll codec tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
