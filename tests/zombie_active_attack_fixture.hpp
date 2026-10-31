#pragma once
#include "zombie_layer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <string>

namespace zombie_attack_fixture {
inline viewer::AnimatedModel humanoid() {
    viewer::AnimatedModel asset;
    const std::array<glm::vec3, 15> points{{
        {0, 1, 0}, {0, 1.25f, 0}, {0, 1.65f, 0},
        {-.22f, 1.45f, 0}, {-.55f, 1.45f, 0}, {.22f, 1.45f, 0}, {.55f, 1.45f, 0},
        {-.12f, 1, 0}, {-.12f, .55f, 0}, {.12f, 1, 0}, {.12f, .55f, 0},
        {-.88f, 1.45f, 0}, {.88f, 1.45f, 0}, {-.12f, .08f, 0}, {.12f, .08f, 0}
    }};
    const std::array<int, 15> parents{{-1, 0, 1, 1, 3, 1, 5, 0, 7, 0, 9, 4, 6, 8, 10}};
    asset.skeleton.resize(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto& bone = asset.skeleton[i];
        bone.name = "bone_" + std::to_string(i); bone.parent = parents[i];
        bone.referenceWorld = glm::translate(glm::mat4(1), points[i]);
        bone.inverseBind = glm::inverse(bone.referenceWorld);
    }
    asset.skeleton[11].name = "bip L Hand"; asset.skeleton[12].name = "bip R Hand";
    asset.skeleton[13].name = "left_foot"; asset.skeleton[14].name = "right_foot";
    for (std::size_t i = 0; i < viewer::ragdollPartCount; ++i) asset.ragdollBones[i] = int(i);
    asset.duration = 1; asset.frameCount = 2;
    for (int frame = 0; frame < 2; ++frame)
        for (const auto& bone : asset.skeleton) asset.boneFrames.push_back(bone.referenceWorld);
    for (bool right : {false, true}) {
        viewer::BakedAnimationClip clip; clip.duration = 1; clip.frameCount = 5; clip.loop = false;
        const auto pivot = points[right ? 5 : 3];
        for (float angle : {0.f, .2f, 1.55f, .25f, 0.f}) {
            const auto arm = glm::translate(glm::mat4(1), pivot) *
                glm::rotate(glm::mat4(1), right ? -angle : angle, glm::vec3(0, 1, 0)) *
                glm::translate(glm::mat4(1), -pivot);
            for (std::size_t bone = 0; bone < points.size(); ++bone) {
                const bool moves = right ? (bone == 5 || bone == 6 || bone == 12) : (bone == 3 || bone == 4 || bone == 11);
                clip.boneFrames.push_back(moves ? arm * asset.skeleton[bone].referenceWorld : asset.skeleton[bone].referenceWorld);
            }
        }
        (right ? asset.attackRightClip : asset.attackLeftClip) = std::move(clip);
    }
    asset.model.boundsMin = {-1, 0, -.2f}; asset.model.boundsMax = {1, 1.9f, 1};
    return asset;
}
} // namespace zombie_attack_fixture
