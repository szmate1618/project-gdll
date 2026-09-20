#include "zombie_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t finger = 0, hand = 1, elbow = 6;

glm::mat4 translation(glm::vec3 position) { return glm::translate(glm::mat4(1), position); }

glm::mat4 around(glm::vec3 center, float radians) {
    return translation(center) * glm::rotate(glm::mat4(1), radians, {0, 0, 1}) * translation(-center);
}

viewer::AnimatedModel humanoid() {
    viewer::AnimatedModel asset;
    asset.duration = 1;
    asset.frameCount = 2;
    // Deliberately put unmapped children before their parents in glTF node order.
    const std::array<glm::vec3, 16> origins{{
        {-0.95f, 1.45f, 0}, {-0.85f, 1.45f, 0},
        {0, 1, 0}, {0, 1.25f, 0}, {0, 1.65f, 0},
        {-0.25f, 1.45f, 0}, {-0.55f, 1.45f, 0},
        {0.25f, 1.45f, 0}, {0.55f, 1.45f, 0},
        {-0.12f, 1, 0}, {-0.12f, 0.55f, 0},
        {0.12f, 1, 0}, {0.12f, 0.55f, 0},
        {0.85f, 1.45f, 0}, {-0.12f, 0.08f, 0}, {0.12f, 0.08f, 0}
    }};
    const std::array<int, 16> parents{{1, 6, -1, 2, 3, 3, 5, 3, 7, 2, 9, 2, 11, 8, 10, 12}};
    asset.skeleton.resize(origins.size());
    for (std::size_t bone = 0; bone < origins.size(); ++bone) {
        auto& node = asset.skeleton[bone];
        node.name = "bone_" + std::to_string(bone);
        node.parent = parents[bone];
        node.referenceWorld = translation(origins[bone]);
        node.inverseBind = glm::inverse(node.referenceWorld);
    }
    asset.skeleton[hand].name = "left_hand";
    asset.skeleton[13].name = "right_hand";
    asset.skeleton[14].name = "left_foot";
    asset.skeleton[15].name = "right_foot";
    for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part)
        asset.ragdollBones[part] = static_cast<int>(part + 2);

    for (int frame = 0; frame < 2; ++frame) {
        const float amount = static_cast<float>(frame);
        const auto body = translation({0.03f * amount, -0.02f * amount, 0});
        const auto arm = around(origins[5], 0.35f + 0.3f * amount);
        const auto wrist = around(origins[hand], -0.3f - 0.2f * amount);
        const auto curl = around(origins[finger], 0.4f + 0.2f * amount);
        for (std::size_t bone = 0; bone < origins.size(); ++bone) {
            auto pose = asset.skeleton[bone].referenceWorld;
            if (bone == finger) pose = curl * pose;
            if (bone == finger || bone == hand) pose = wrist * pose;
            if (bone == finger || bone == hand || bone == 5 || bone == elbow) pose = arm * pose;
            asset.boneFrames.push_back(body * pose);
        }
    }

    viewer::Primitive primitive;
    viewer::AnimatedPrimitive animation;
    for (std::size_t bone = 0; bone < origins.size(); ++bone) {
        viewer::Vertex vertex;
        vertex.position = origins[bone] + glm::vec3(0.03f, 0.02f, 0.01f);
        primitive.vertices.push_back(vertex);
        viewer::SkinningVertex skin;
        skin.joints[0] = static_cast<std::uint32_t>(bone);
        skin.weights[0] = 1;
        animation.skinning.push_back(skin);
    }
    auto blendedVertex = primitive.vertices[hand];
    blendedVertex.position.x += 0.04f;
    primitive.vertices.push_back(blendedVertex);
    viewer::SkinningVertex blended;
    blended.joints[0] = elbow;
    blended.weights[0] = 0.35f;
    blended.joints[4] = hand;
    blended.weights[4] = 0.45f;
    blended.joints[5] = finger;
    blended.weights[5] = 0.2f;
    animation.skinning.push_back(blended);
    for (std::size_t frame = 0; frame < 2; ++frame) {
        for (std::size_t v = 0; v < primitive.vertices.size(); ++v) {
            glm::vec4 position(0);
            for (std::size_t influence = 0; influence < 8; ++influence) {
                const auto& skin = animation.skinning[v];
                const auto joint = skin.joints[influence];
                position += asset.boneFrames[frame * origins.size() + joint] * asset.skeleton[joint].inverseBind *
                    glm::vec4(primitive.vertices[v].position, 1) * skin.weights[influence];
            }
            animation.frames.push_back({position, {0, 1, 0, 0}});
        }
    }
    asset.model.primitives.push_back(std::move(primitive));
    asset.animation.push_back(std::move(animation));
    return asset;
}

glm::mat4 worldBone(const viewer::ZombieInstance& instance, const viewer::AnimatedModel& asset,
                     std::size_t bone) {
    return instance.transform * instance.ragdollBones[bone] * glm::inverse(asset.skeleton[bone].inverseBind);
}

void requireMatrixNear(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
    for (int column = 0; column < 4; ++column)
        require(glm::length(actual[column] - expected[column]) < 2e-4f, message);
}

void checkTransition(double seconds, float phase, std::size_t first, std::size_t second, float fraction) {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    const auto& asset = layer.assets[0];
    viewer::ZombieInstance instance;
    instance.restRoot = translation({13, 4, -19}) * glm::rotate(glm::mat4(1), -0.9f, {0, 1, 0});
    instance.restTransform = instance.restRoot * glm::scale(glm::mat4(1), glm::vec3(1.25f)) *
                             translation({0.2f, -0.05f, -0.15f});
    instance.transform = instance.restTransform;
    instance.phase = phase;
    layer.instances.push_back(instance);
    const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
    require(layer.shoot(center + glm::vec3(0, 0, 3), {0, 0, -1}, seconds), "Synthetic humanoid must be hit");
    const auto& ragdoll = layer.instances[0];
    require(ragdoll.ragdoll && ragdoll.activationPose.size() == asset.skeleton.size(),
            "Activation must capture the complete displayed skeleton");
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone) {
        const auto expected = asset.boneFrames[first * asset.skeleton.size() + bone] * (1 - fraction) +
                              asset.boneFrames[second * asset.skeleton.size() + bone] * fraction;
        requireMatrixNear(ragdoll.activationPose[bone], expected, "Activation must capture the requested time and phase");
    }
    const auto& vertices = asset.model.primitives[0].vertices;
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        glm::vec4 skinned(0);
        const auto& skin = asset.animation[0].skinning[v];
        for (std::size_t influence = 0; influence < 8; ++influence)
            skinned += ragdoll.ragdollBones[skin.joints[influence]] * glm::vec4(vertices[v].position, 1) *
                       skin.weights[influence];
        const auto baked = asset.animation[0].frames[first * vertices.size() + v].position * (1 - fraction) +
                           asset.animation[0].frames[second * vertices.size() + v].position * fraction;
        require(glm::length(ragdoll.transform * skinned - instance.restTransform * baked) < 2e-4f,
                "Activation must not move displayed vertices, including blended and unmapped joints");
    }

    const auto capturedHand = glm::inverse(ragdoll.activationPose[elbow]) * ragdoll.activationPose[hand];
    const auto capturedFinger = glm::inverse(ragdoll.activationPose[hand]) * ragdoll.activationPose[finger];
    const auto initialElbow = worldBone(ragdoll, asset, elbow);
    for (int step = 0; step < 30; ++step) {
        layer.update(1.0f / 120);
        const auto handWorld = worldBone(ragdoll, asset, hand);
        const auto elbowWorld = worldBone(ragdoll, asset, elbow);
        const auto fingerWorld = worldBone(ragdoll, asset, finger);
        requireMatrixNear(glm::inverse(elbowWorld) * handWorld, capturedHand,
                          "Unmapped hand must follow its moving parent in the captured pose");
        requireMatrixNear(glm::inverse(handWorld) * fingerWorld, capturedFinger,
                          "Unmapped finger must preserve its captured curl through multiple ancestors");
    }
    require(glm::length(worldBone(ragdoll, asset, elbow)[3] - initialElbow[3]) > 0.01f,
            "The parent must actually move while checking inherited hand and finger poses");
}

void unknownRigFollowsFallbackMotion() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    auto& asset = layer.assets[0];
    asset.ragdollBones.fill(-1);
    viewer::ZombieInstance instance;
    instance.restRoot = translation({5, 2, 3}) * glm::rotate(glm::mat4(1), 0.6f, {0, 1, 0});
    instance.restTransform = instance.restRoot * glm::scale(glm::mat4(1), glm::vec3(1.3f));
    instance.transform = instance.restTransform;
    layer.instances.push_back(instance);
    const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
    require(layer.shoot(center + glm::vec3(0, 0, 3), {0, 0, -1}, 0.25),
            "An unmapped rig must still support fallback activation");
    const auto& ragdoll = layer.instances[0];
    constexpr std::size_t root = 2; // The fixture's root has one fully weighted vertex.
    const auto& vertices = asset.model.primitives[0].vertices;
    const glm::vec4 vertex(vertices[root].position, 1);
    const auto baked = (asset.animation[0].frames[root].position +
                        asset.animation[0].frames[vertices.size() + root].position) * 0.5f;
    const auto initial = ragdoll.transform * ragdoll.ragdollBones[root] * vertex;
    require(glm::length(initial - instance.restTransform * baked) < 2e-4f,
            "Fallback activation must preserve the displayed root-bound vertex");
    for (int step = 0; step < 30; ++step) {
        layer.update(1.0f / 120);
        const auto actual = ragdoll.transform * ragdoll.ragdollBones[root] * vertex;
        require(glm::length(actual - ragdoll.transform * baked) < 2e-4f,
                "An unmapped root must follow fallback instance motion instead of freezing in world space");
    }
    require(glm::length(ragdoll.transform * ragdoll.ragdollBones[root] * vertex - initial) > 0.01f,
            "Fallback physics must actually move the visible root-bound vertex");
}

void unmatchedRigFollowsFallbackBody() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    auto& asset = layer.assets[0];
    asset.ragdollBones.fill(-1);
    viewer::ZombieInstance instance;
    instance.restRoot = translation({3, 0, -7});
    instance.transform = instance.restTransform = instance.restRoot;
    instance.phase = 0.125f;
    layer.instances.push_back(instance);
    const auto expectedPose = viewer::sampleAnimatedPose(asset, 0.125, instance.phase);
    require(layer.shoot({3, 0.9f, -4}, {0, 0, -1}, 0.125), "Unknown rig must use the fallback body");
    const auto& ragdoll = layer.instances[0];
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
        requireMatrixNear(worldBone(ragdoll, asset, bone), instance.transform * expectedPose[bone],
                          "Unknown rigs must also preserve their captured pose at activation");
    const auto initialBones = ragdoll.ragdollBones;
    const auto initialHand = worldBone(ragdoll, asset, hand);
    const auto initialTransform = ragdoll.transform;
    for (int step = 0; step < 30; ++step) layer.update(1.0f / 60);
    const auto motion = ragdoll.transform[3] - initialTransform[3];
    require(glm::length(motion) > 0.01f, "Fallback physics must move the character");
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
        requireMatrixNear(ragdoll.ragdollBones[bone], initialBones[bone],
                          "An unmatched skeleton must retain its pose relative to the moving instance");
    require(glm::length(worldBone(ragdoll, asset, hand)[3] - initialHand[3] - motion) < 2e-4f,
            "Unmapped skeleton roots must follow the body rather than freeze in world space");
}

} // namespace

int main() {
    try {
        checkTransition(0.125, 0.125f, 0, 1, 0.5f);
        checkTransition(0.625, 0.25f, 1, 0, 0.75f);
        checkTransition(1e12 + 0.125, 0.125f, 0, 1, 0.5f);
        unmatchedRigFollowsFallbackBody();
        unknownRigFollowsFallbackMotion();
        std::cout << "Zombie activation transition tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie transition test failed: " << error.what() << '\n';
        return 1;
    }
}
